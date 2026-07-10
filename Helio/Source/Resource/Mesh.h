/// @file Mesh.h
/// @brief GPU-resident mesh + `MeshSystem` factory.
///
/// `MeshSystem::CreateMesh(MeshDesc)` takes a CPU-authored `MeshData`, runs
/// meshoptimizer over it (vertex-cache → overdraw → vertex-fetch), uploads
/// to two storage buffers (one interleaved Vertex[], one packed index buffer),
/// and returns a `Mesh` handle.
///
/// The Mesh handle is plain-old-data — you pass it by value, copy it freely.
/// `MeshSystem` owns the GPU resources and frees them when destroyed.
///
/// **Future extensions** (intentionally NOT in V1):
///   - Meshlet generation (Phase 13+ for cluster culling / mesh shaders)
///   - LOD chain via `meshopt_simplify`
///   - SDF generation (separate compute baker)
///   - meshopt vertex/index compression (`.helmesh` cache format)
///   - Auto-BLAS build hook
/// When these land, the Mesh struct will grow new fields. Existing code that
/// only reads VertexBuffer/IndexBuffer/IndexCount won't need to change.
#pragma once

#include "Material.h"
#include "MeshData.h"
#include "TextureCache.h"

#include <Core/Math/Math.h>

#include <RHI/Public/Buffer.h>
#include <RHI/Public/CommandList.h>

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace helio::rhi { class Device; }

namespace helio::resource {

/// Build-time statistics captured by `CreateMesh`. Surfaced via logs and
/// available on the returned Mesh for HUD overlays.
struct MeshStats {
    uint32_t VertexCount;
    uint32_t TriangleCount;
    /// True if `MeshDesc::Optimize` was honored (false on tiny meshes where
    /// optimization is skipped or if meshopt failed).
    bool     Optimized;
    /// Wall-clock ms spent in meshopt + upload. Useful for caching decisions.
    float    BuildMs;
};

/// GPU-resident mesh. POD; copy by value freely.
struct Mesh {
    uint64_t Id{0};

    rhi::BufferHandle  VertexBuffer{};   // interleaved Vertex[]
    rhi::BufferHandle  IndexBuffer{};    // u16 or u32 packed
    uint32_t           VertexCount{0};
    uint32_t           IndexCount{0};
    IndexFormat        Indices{IndexFormat::U32};
    math::AABB         Bounds{};

    MeshStats          Stats{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Id != 0; }
};

    
    
/// Convenience: pick the right `IndexType` enum for `CommandList::BindIndexBuffer`.
[[nodiscard]] inline rhi::IndexType IndexTypeFor(const Mesh& M) noexcept {
    return M.Indices == IndexFormat::U16 ? rhi::IndexType::U16 : rhi::IndexType::U32;
}

/// A drawable unit: one `Mesh` paired with the `Material` it renders with.
///
/// This is the granularity a `StaticMeshActor` holds and the renderer draws.
/// It lives in the resource layer (not scene) because the importer — which
/// reads glTF material data and builds the `Material` — is a resource-level
/// system, and the mesh+material bundle IS the imported asset's material
/// assignment (the same way a mesh asset's sections carry material slots in
/// other engines). A glTF file with N primitives imports to N sections.
struct MeshSection {
    std::string SectionName;  // for debugging; not used by the renderer
    Mesh     Mesh;
    Material Material;
    /// Placement of this section RELATIVE to its owning actor, in Helio space.
    /// `LoadModel` bakes each glTF node's world transform here so a multi-object
    /// file reconstructs its authored layout under a single actor. Identity for
    /// hand-built single-mesh actors (the whole actor moves as one). The actor
    /// submits `actorWorld * LocalTransform` per section.
    float4x4 LocalTransform = float4x4::identity();
};

/// Authoring options passed to `MeshSystem::CreateMesh`.
struct MeshDesc {
    /// Source CPU data. Pointer (not span/value) so callers can build a
    /// MeshData in-place and pass `&data`. CreateMesh consumes once.
    const MeshData* Data{nullptr};

    /// Apply meshopt's vertex-cache → overdraw → vertex-fetch pipeline.
    /// Default true; turn off only for very tiny meshes (verts < 8) where
    /// optimization is pointless and the calls just churn.
    bool Optimize{true};

    /// Build a BLAS for this mesh's geometry, suitable for adding to a TLAS.
    /// Requires `Device::HasRayTracing()`. Result goes... nowhere in V1 (the
    /// hook returns the BLAS handle separately when meshlets/RT integration
    /// lands). Setting `true` today just logs a warning.
    bool BuildBLAS{false};

    /// Debug name surfaced in RenderDoc + VMA. Prefixes both vertex + index
    /// buffer names.
    const char* DebugName{nullptr};
};

/// Owns GPU mesh storage. One instance per game; create at boot, destroy at
/// shutdown. Holding a `Mesh` handle past the MeshSystem's lifetime is UB.
class MeshSystem {
public:
    explicit MeshSystem(rhi::Device& Dev);
    ~MeshSystem();

    MeshSystem(const MeshSystem&) = delete;
    MeshSystem& operator=(const MeshSystem&) = delete;

    /// Upload + optimize + register a mesh. Returns an invalid Mesh on failure
    /// (logs the reason). Pass `Data.Vertices` and `Data.Indices` already
    /// populated; `Data.Bounds` will be recomputed if empty.
    [[nodiscard]] Mesh CreateMesh(const MeshDesc& Desc);

    /// Import + create every triangle primitive from a `.gltf` / `.glb` file
    /// (via `ImportGltf`), pairing each with the `Material` parsed from its
    /// glTF material — one `MeshSection` per primitive, ready to hand to a
    /// `StaticMeshActor`. This is the "set it and forget it" load path: the
    /// material assignment (factors AND textures) travels with the mesh.
    /// Material textures are decoded + uploaded through this system's owned
    /// `TextureCache`. Returns empty on load failure (logs the reason). See
    /// `MeshImport.h` for the coordinate conversion.
    [[nodiscard]] std::vector<MeshSection> LoadModel(const std::filesystem::path& Path);

    /// Geometry-only variant of `LoadModel`: creates the meshes but drops the
    /// materials (and does not load textures). Use when you'll assign
    /// materials yourself.
    [[nodiscard]] std::vector<Mesh> LoadMeshes(const std::filesystem::path& Path);

    /// Schedule the mesh's buffers for deferred destruction (next frame the
    /// slot retires). Safe to call from anywhere except inside a render pass.
    void DestroyMesh(Mesh M);

    /// The texture cache this system owns (material textures from `LoadModel`).
    [[nodiscard]] TextureCache& Textures() noexcept { return m_textureCache; }

private:
    rhi::Device* m_dev{nullptr};
    std::unordered_map<uint64_t, Mesh> m_meshes;
    uint64_t m_nextId{1};
    TextureCache m_textureCache;
};

} // namespace helio::resource
