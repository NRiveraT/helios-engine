#include "MeshImport.h"

#include "TextureCache.h"

#include <Core/Logging/Log.h>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <execution>
#include <map>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace helio::resource {

namespace {

// ---- glTF image byte resolution --------------------------------------------

struct ByteSpan { const std::byte* Data; size_t Size; };

// Bytes that are already resident in memory (external files loaded via
// Options::LoadExternalImages, base64 data-URIs, or ByteView spans).
std::optional<ByteSpan> DirectBytes(const fastgltf::DataSource& Src) {
    if (const auto* A = std::get_if<fastgltf::sources::Array>(&Src)) {
        return ByteSpan{A->bytes.data(), A->bytes.size()};
    }
    if (const auto* V = std::get_if<fastgltf::sources::Vector>(&Src)) {
        return ByteSpan{V->bytes.data(), V->bytes.size()};
    }
    if (const auto* B = std::get_if<fastgltf::sources::ByteView>(&Src)) {
        return ByteSpan{B->bytes.data(), B->bytes.size()};
    }
    return std::nullopt;
}

// Compressed bytes for a glTF image, resolving the three storage shapes:
// direct in-memory, or a slice of a (GLB / external) buffer via a buffer view.
std::optional<ByteSpan> ImageBytes(const fastgltf::Asset& Asset, const fastgltf::Image& Img) {
    if (auto Direct = DirectBytes(Img.data)) {
        return Direct;
    }
    if (const auto* BV = std::get_if<fastgltf::sources::BufferView>(&Img.data)) {
        const fastgltf::BufferView& View = Asset.bufferViews[BV->bufferViewIndex];
        const fastgltf::Buffer& Buffer = Asset.buffers[View.bufferIndex];
        if (auto Base = DirectBytes(Buffer.data)) {
            if (View.byteOffset + View.byteLength <= Base->Size) {
                return ByteSpan{Base->Data + View.byteOffset, View.byteLength};
            }
        }
    }
    return std::nullopt;
}

// Resolve a material texture reference to the underlying glTF image index
// (textureInfo -> texture -> image). Returns nullopt when absent/invalid.
std::optional<size_t> ResolveImageIndex(const fastgltf::Asset& Asset,
                                        const fastgltf::TextureInfo& Info) {
    if (Info.textureIndex >= Asset.textures.size()) {
        return std::nullopt;
    }
    const fastgltf::Texture& Tex = Asset.textures[Info.textureIndex];
    if (!Tex.imageIndex.has_value() || *Tex.imageIndex >= Asset.images.size()) {
        return std::nullopt;
    }
    return *Tex.imageIndex;
}

// Which Material slot a texture request targets.
enum class TexSlot { Albedo, Normal, MetalRough, Emissive, Occlusion };

// A deferred texture load: fill `Out[OutIndex].Material`'s `Slot` from the
// (image, colorspace) once all images have been decoded + uploaded.
struct TexRequest {
    size_t   OutIndex;
    TexSlot  Slot;
    size_t   ImageIdx;
    bool     sRGB;
};

void AssignSlot(Material& M, TexSlot Slot, uint32_t Value) {
    switch (Slot) {
        case TexSlot::Albedo:     M.AlbedoTex = Value; break;
        case TexSlot::Normal:     M.NormalTex = Value; break;
        case TexSlot::MetalRough: M.MetalRoughTex = Value; break;
        case TexSlot::Emissive:   M.EmissiveTex = Value; break;
        case TexSlot::Occlusion:  M.OcclusionTex = Value; break;
    }
}

// glTF is right-handed (-Z forward); Helio is left-handed (+Z forward). Negate
// Z to flip handedness. (Winding is reversed separately, in the index copy.)
float3 GltfToHelio(fastgltf::math::fvec3 V) {
    return float3(V.x(), V.y(), -V.z());
}

// Recompute flat per-triangle normals when the source has none. Uses Helio's
// left-handed winding (front faces are CCW after the winding reversal), so the
// cross product points outward for front-facing triangles.
void ComputeFlatNormals(MeshData& M) {
    for (auto& V : M.Vertices) { V.Normal[0] = V.Normal[1] = V.Normal[2] = 0.0f; }
    for (size_t I = 0; I + 2 < M.Indices.size(); I += 3) {
        const uint32_t A = M.Indices[I], B = M.Indices[I + 1], C = M.Indices[I + 2];
        const float3 P0 = GetPos(M.Vertices[A]);
        const float3 P1 = GetPos(M.Vertices[B]);
        const float3 P2 = GetPos(M.Vertices[C]);
        const float3 N = hlslpp::cross(P1 - P0, P2 - P0);
        for (uint32_t Idx : {A, B, C}) {
            M.Vertices[Idx].Normal[0] += float(N.x);
            M.Vertices[Idx].Normal[1] += float(N.y);
            M.Vertices[Idx].Normal[2] += float(N.z);
        }
    }
    for (auto& V : M.Vertices) {
        float3 N = GetNormal(V);
        if (float(hlslpp::dot(N, N)) > 1e-12f) {
            SetNormal(V, hlslpp::normalize(N));
        } else {
            SetNormal(V, float3(0.0f, 1.0f, 0.0f));
        }
    }
}

// Generate per-vertex tangents from positions + UVs (Lengyel's method) when the
// source has none — required for tangent-space normal mapping. Runs AFTER the
// Z-negation + winding reversal, so the basis is consistent with the imported
// geometry; the bitangent sign (Tangent.w) is derived, not assumed. Assumes
// normals and indices are already populated.
void ComputeTangents(MeshData& M) {
    const size_t N = M.Vertices.size();
    std::vector<float3> Tan(N, float3(0.0f, 0.0f, 0.0f));
    std::vector<float3> Bitan(N, float3(0.0f, 0.0f, 0.0f));

    for (size_t I = 0; I + 2 < M.Indices.size(); I += 3) {
        const uint32_t A = M.Indices[I], B = M.Indices[I + 1], C = M.Indices[I + 2];
        const float3 P0 = GetPos(M.Vertices[A]);
        const float3 P1 = GetPos(M.Vertices[B]);
        const float3 P2 = GetPos(M.Vertices[C]);
        const float2 U0 = GetUV(M.Vertices[A]);
        const float2 U1 = GetUV(M.Vertices[B]);
        const float2 U2 = GetUV(M.Vertices[C]);

        const float3 E1 = P1 - P0, E2 = P2 - P0;
        const float DU1 = float(U1.x) - float(U0.x), DV1 = float(U1.y) - float(U0.y);
        const float DU2 = float(U2.x) - float(U0.x), DV2 = float(U2.y) - float(U0.y);
        const float Det = DU1 * DV2 - DU2 * DV1;
        if (std::abs(Det) < 1e-12f) {
            continue; // degenerate UVs — contributes no tangent
        }
        const float R = 1.0f / Det;
        const float3 T = (E1 * DV2 - E2 * DV1) * R;
        const float3 Bt = (E2 * DU1 - E1 * DU2) * R;
        for (uint32_t Idx : {A, B, C}) {
            Tan[Idx] += T;
            Bitan[Idx] += Bt;
        }
    }

    for (size_t V = 0; V < N; ++V) {
        const float3 Nrm = GetNormal(M.Vertices[V]);
        float3 T = Tan[V];
        // Gram-Schmidt: orthogonalize the tangent against the normal.
        T = T - Nrm * float(hlslpp::dot(Nrm, T));
        if (float(hlslpp::dot(T, T)) < 1e-12f) {
            // Degenerate — pick any axis perpendicular to the normal.
            T = std::abs(float(Nrm.y)) > 0.999f ? float3(1.0f, 0.0f, 0.0f)
                                                : hlslpp::cross(float3(0.0f, 1.0f, 0.0f), Nrm);
        }
        T = hlslpp::normalize(T);
        // Handedness: sign of the bitangent relative to cross(N,T).
        const float W = float(hlslpp::dot(hlslpp::cross(Nrm, T), Bitan[V])) < 0.0f ? -1.0f : 1.0f;
        SetTangent(M.Vertices[V], float4(float(T.x), float(T.y), float(T.z), W));
    }
}

} // namespace

std::vector<ImportedMesh> ImportGltf(const std::filesystem::path& Path, TextureCache* Textures) {
    std::vector<ImportedMesh> Out;

    auto DataBuffer = fastgltf::GltfDataBuffer::FromPath(Path);
    if (DataBuffer.error() != fastgltf::Error::None) {
        HELIO_LOG_WARN("Resource", "ImportGltf: cannot read '{}' ({})",
                       Path.string(), fastgltf::getErrorMessage(DataBuffer.error()));
        return Out;
    }

    // LoadExternalBuffers resolves .bin sidecars; GenerateMeshIndices supplies
    // indices for any non-indexed primitive so our uniform indexed draw path
    // always applies; LoadExternalImages pulls referenced image files into
    // memory so the texture path can decode them (only needed when loading
    // textures).
    auto Options = fastgltf::Options::LoadExternalBuffers |
                   fastgltf::Options::GenerateMeshIndices;
    if (Textures != nullptr) {
        Options |= fastgltf::Options::LoadExternalImages;
    }

    fastgltf::Parser Parser;
    auto Asset = Parser.loadGltf(DataBuffer.get(), Path.parent_path(), Options);
    if (Asset.error() != fastgltf::Error::None) {
        HELIO_LOG_WARN("Resource", "ImportGltf: parse failed for '{}' ({})", Path.string(), fastgltf::getErrorMessage(Asset.error()));
        return Out;
    }

    // Deferred texture loads — collected during material parsing, then decoded
    // in parallel + uploaded after the mesh loop (see below). Decode is the
    // load bottleneck (CPU, ~100ms/image) and is embarrassingly parallel.
    std::vector<TexRequest> TexRequests;

    for (const fastgltf::Mesh& Mesh : Asset->meshes) {
        uint32_t PrimIndex = 0;
        for (const fastgltf::Primitive& Prim : Mesh.primitives) {
            if (Prim.type != fastgltf::PrimitiveType::Triangles) {
                continue; // points/lines aren't renderable by the mesh pipeline
            }
            const auto* PosAttr = Prim.findAttribute("POSITION");
            if (PosAttr == Prim.attributes.end()) {
                continue; // positionless primitive — skip
            }

            MeshData Data;
            const fastgltf::Accessor& PosAcc = Asset->accessors[PosAttr->accessorIndex];
            Data.Vertices.resize(PosAcc.count);

            // POSITION (required). Default the rest; overwrite where present.
            for (auto& V : Data.Vertices) {
                V.UV[0] = V.UV[1] = 0.0f;
                V.Tangent[0] = 1.0f; V.Tangent[1] = 0.0f; V.Tangent[2] = 0.0f; V.Tangent[3] = 1.0f;
            }
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                Asset.get(), PosAcc, [&](fastgltf::math::fvec3 P, size_t I) {
                    SetPos(Data.Vertices[I], GltfToHelio(P));
                });

            bool HasNormals = false;
            if (const auto* N = Prim.findAttribute("NORMAL"); N != Prim.attributes.end()) {
                HasNormals = true;
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                    Asset.get(), Asset->accessors[N->accessorIndex],
                    [&](fastgltf::math::fvec3 Nml, size_t I) {
                        SetNormal(Data.Vertices[I], hlslpp::normalize(GltfToHelio(Nml)));
                    });
            }

            if (const auto* T = Prim.findAttribute("TEXCOORD_0"); T != Prim.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                    Asset.get(), Asset->accessors[T->accessorIndex],
                    [&](fastgltf::math::fvec2 UV, size_t I) {
                        Data.Vertices[I].UV[0] = UV.x();
                        Data.Vertices[I].UV[1] = UV.y();
                    });
            }

            bool HasTangents = false;
            if (const auto* T = Prim.findAttribute("TANGENT"); T != Prim.attributes.end()) {
                HasTangents = true;
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                    Asset.get(), Asset->accessors[T->accessorIndex],
                    [&](fastgltf::math::fvec4 Tan, size_t I) {
                        // Negate Z (handedness) and flip the bitangent sign w:
                        // mirroring one axis flips the basis handedness, so w
                        // must flip to keep cross(N,T)*w pointing the same way.
                        SetTangent(Data.Vertices[I],
                                   float4(Tan.x(), Tan.y(), -Tan.z(), -Tan.w()));
                    });
            }

            // Indices (GenerateMeshIndices guarantees presence). Reverse each
            // triangle's winding to compensate for the Z negation, so front
            // faces stay CCW in Helio's left-handed world.
            if (Prim.indicesAccessor.has_value()) {
                const fastgltf::Accessor& IdxAcc = Asset->accessors[*Prim.indicesAccessor];
                Data.Indices.reserve(IdxAcc.count);
                fastgltf::iterateAccessor<std::uint32_t>(
                    Asset.get(), IdxAcc, [&](std::uint32_t Index) {
                        Data.Indices.push_back(Index);
                    });
                for (size_t I = 0; I + 2 < Data.Indices.size(); I += 3) {
                    std::swap(Data.Indices[I + 1], Data.Indices[I + 2]);
                }
            }

            if (Data.Vertices.empty() || Data.Indices.empty()) {
                continue;
            }
            if (!HasNormals) {
                ComputeFlatNormals(Data);
            }
            if (!HasTangents) {
                ComputeTangents(Data); // needs normals + indices in place
            }
            Data.RecomputeBounds();

            ImportedMesh Imported;
            Imported.Name = Mesh.name.empty()
                                ? "gltf_mesh"
                                : std::string(Mesh.name.begin(), Mesh.name.end());
            if (Mesh.primitives.size() > 1) {
                Imported.Name += "." + std::to_string(PrimIndex);
            }

            // Material factors -> resource::Material. glTF's metallic-roughness
            // model maps 1:1 onto our material. Textures (when a TextureCache is
            // supplied) are decoded + uploaded and their slots stored; sRGB for
            // color maps (base color, emissive), linear for data maps (normal,
            // metallic-roughness, occlusion). A primitive with no material keeps
            // the defaults.
            if (Prim.materialIndex.has_value() &&
                *Prim.materialIndex < Asset->materials.size()) {
                const fastgltf::Material& GMat = Asset->materials[*Prim.materialIndex];
                const auto& Base = GMat.pbrData.baseColorFactor;
                Imported.Material.AlbedoTint = float3(Base[0], Base[1], Base[2]);
                Imported.Material.Metallic = GMat.pbrData.metallicFactor;
                Imported.Material.Roughness = GMat.pbrData.roughnessFactor;
                const auto& Emi = GMat.emissiveFactor;
                Imported.Material.EmissiveColor = float3(Emi[0], Emi[1], Emi[2]);
                Imported.Material.EmissiveIntensity = GMat.emissiveStrength;

                if (Textures != nullptr) {
                    // Record (don't load yet) — Out.size() is this mesh's index
                    // once it's pushed at the end of the loop body.
                    const size_t OutIdx = Out.size();
                    const auto Record = [&](const fastgltf::TextureInfo& Info, TexSlot Slot, bool sRGB) {
                        if (auto Idx = ResolveImageIndex(Asset.get(), Info)) {
                            TexRequests.push_back({OutIdx, Slot, *Idx, sRGB});
                        }
                    };
                    if (GMat.pbrData.baseColorTexture.has_value())
                        Record(*GMat.pbrData.baseColorTexture, TexSlot::Albedo, true);
                    if (GMat.pbrData.metallicRoughnessTexture.has_value())
                        Record(*GMat.pbrData.metallicRoughnessTexture, TexSlot::MetalRough, false);
                    if (GMat.normalTexture.has_value())
                        Record(*GMat.normalTexture, TexSlot::Normal, false);
                    if (GMat.emissiveTexture.has_value())
                        Record(*GMat.emissiveTexture, TexSlot::Emissive, true);
                    if (GMat.occlusionTexture.has_value())
                        Record(*GMat.occlusionTexture, TexSlot::Occlusion, false);
                }
            }

            Imported.Data = std::move(Data);
            Out.push_back(std::move(Imported));
            ++PrimIndex;
        }
    }

    // ---- Texture load: parallel decode, serial upload ------------------------
    // Image decode (PNG/JPG) dominates load time (~100 ms each, CPU) and is
    // independent per image, so decode all UNIQUE images across worker threads
    // (std::execution::par), then upload serially on this thread (uploads are
    // cheap and Vulkan submission isn't thread-safe). De-duplicated by
    // (image, colorspace): a shared image decodes/uploads once. Processed in
    // chunks so peak host memory stays bounded to ~one chunk of decoded images
    // rather than the whole set at once.
    if (Textures != nullptr && !TexRequests.empty()) {
        struct Job { size_t ImageIdx; bool sRGB; DecodedImage Image; uint32_t Slot = kNoTexture; };
        std::map<std::pair<size_t, bool>, size_t> JobOf; // (image, sRGB) -> Jobs index
        std::vector<Job> Jobs;
        for (const TexRequest& R : TexRequests) {
            const auto Key = std::make_pair(R.ImageIdx, R.sRGB);
            if (JobOf.emplace(Key, Jobs.size()).second) {
                Jobs.push_back({R.ImageIdx, R.sRGB, {}, kNoTexture});
            }
        }

        const auto TStart = std::chrono::steady_clock::now();
        // Chunk size trades peak host memory (≈ one chunk of decoded images)
        // against thread utilization + barrier count. Match the core count so
        // every worker is fed, clamped so a huge-core machine with large
        // textures doesn't spike memory.
        const unsigned Cores = std::max(std::thread::hardware_concurrency(), 4u);
        const size_t Chunk = std::min<size_t>(Cores, 32u);
        for (size_t Base = 0; Base < Jobs.size(); Base += Chunk) {
            const size_t End = std::min(Base + Chunk, Jobs.size());
            // Decode this chunk in parallel (thread-safe; reads the asset,
            // writes only into its own Job).
            std::for_each(std::execution::par, Jobs.begin() + Base, Jobs.begin() + End,
                          [&Asset](Job& J) {
                              if (auto Bytes = ImageBytes(Asset.get(), Asset->images[J.ImageIdx])) {
                                  J.Image = TextureCache::Decode(Bytes->Data, Bytes->Size, "gltf_image");
                              }
                          });
            // Upload this chunk serially, then free its host pixels.
            for (size_t I = Base; I < End; ++I) {
                Jobs[I].Slot = Textures->Upload(Jobs[I].Image, Jobs[I].sRGB, "gltf_image");
                Jobs[I].Image.Pixels = {}; // release decoded memory promptly
            }
        }
        const double LoadMs = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - TStart).count();

        // Fan the resolved slots back out to the materials that requested them.
        for (const TexRequest& R : TexRequests) {
            const uint32_t Slot = Jobs[JobOf[std::make_pair(R.ImageIdx, R.sRGB)]].Slot;
            AssignSlot(Out[R.OutIndex].Material, R.Slot, Slot);
        }
        HELIO_LOG_INFO("Resource",
                       "ImportGltf: {} unique textures decoded+uploaded in {:.0f} ms ({} threads)",
                       Jobs.size(), LoadMs, std::thread::hardware_concurrency());
    }

    if (Out.empty()) {
        HELIO_LOG_WARN("Resource", "ImportGltf: '{}' contained no triangle geometry", Path.string());
    } else {
        HELIO_LOG_INFO("Resource", "ImportGltf: '{}' -> {} primitive(s)", Path.string(), Out.size());
    }
    return Out;
}

} // namespace helio::resource
