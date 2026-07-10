#include "Mesh.h"

#include "MeshImport.h"

#include <RHI/Public/Device.h>

#include <Core/Assert/Assert.h>
#include <Core/Logging/Log.h>
#include <Core/Time/Clock.h>

#include <meshoptimizer.h>

#include <vector>

namespace helio::resource {

MeshSystem::MeshSystem(rhi::Device& Dev) : m_dev(&Dev), m_textureCache(Dev) {}

std::vector<MeshSection> MeshSystem::LoadModel(const std::filesystem::path& Path) {
    ImportedScene Scene = ImportGltf(Path, &m_textureCache);

    // Create one GPU mesh per imported primitive, grouped by the glTF mesh it
    // came from. Nodes that reference the same mesh index then reuse the SAME
    // Mesh handles — that shared identity is what lets the renderer collapse
    // them into one instanced draw (batching keys on Mesh.Id).
    struct Prim { Mesh Mesh; Material Material; std::string Name; };
    std::unordered_map<size_t, std::vector<Prim>> ByMesh;
    for (ImportedMesh& Imported : Scene.Primitives) {
        Mesh M = CreateMesh({.Data = &Imported.Data, .DebugName = Imported.Name.c_str()});
        if (M.IsValid()) {
            ByMesh[Imported.SourceMeshIndex].push_back(
                {M, std::move(Imported.Material), std::move(Imported.Name)});
        }
    }

    std::vector<MeshSection> Sections;

    // No scene graph (or a file that placed nothing): one section per primitive
    // at the origin — the pre-hierarchy behavior.
    if (Scene.Nodes.empty()) {
        for (auto& [MeshIndex, Prims] : ByMesh) {
            for (Prim& P : Prims) {
                Sections.push_back({P.Name, P.Mesh, P.Material, float4x4::identity()});
            }
        }
        return Sections;
    }

    // One section per (placement × primitive of its mesh), at the node's baked
    // world transform. Shared handles + equal materials collapse downstream.
    for (const ImportedNode& Node : Scene.Nodes) {
        const auto It = ByMesh.find(Node.MeshIndex);
        if (It == ByMesh.end()) {
            continue; // node referenced a mesh with no renderable triangle geometry
        }
        for (const Prim& P : It->second) {
            Sections.push_back({P.Name, P.Mesh, P.Material, Node.LocalToRoot});
        }
    }
    return Sections;
}

std::vector<Mesh> MeshSystem::LoadMeshes(const std::filesystem::path& Path) {
    std::vector<Mesh> Result;
    for (const MeshSection& Section : LoadModel(Path)) {
        Result.push_back(Section.Mesh);
    }
    return Result;
}

MeshSystem::~MeshSystem() {
    for (auto& [Id, M] : m_meshes) {
        if (M.VertexBuffer.IsValid()) m_dev->DestroyBuffer(M.VertexBuffer);
        if (M.IndexBuffer.IsValid())  m_dev->DestroyBuffer(M.IndexBuffer);
    }
    m_meshes.clear();
}

Mesh MeshSystem::CreateMesh(const MeshDesc& Desc) {
    HELIO_CHECK(Desc.Data);
    const MeshData& Src = *Desc.Data;
    HELIO_CHECK(!Src.Vertices.empty() && !Src.Indices.empty());
    HELIO_CHECK(Src.Indices.size() % 3 == 0);

    core::Clock BuildClock;

    // ---- Working copies (we mutate via meshopt) ---------------------------
    std::vector<Vertex>   Verts = Src.Vertices;
    std::vector<uint32_t> Idx   = Src.Indices;
    bool Optimized = false;

    if (Desc.Optimize && Verts.size() >= 8) {
        // Pre-pass: remove duplicate vertices via meshopt's remap. Bounded
        // by index count.
        std::vector<uint32_t> Remap(Idx.size());
        const size_t UniqueCount = meshopt_generateVertexRemap(
            Remap.data(),
            Idx.data(), Idx.size(),
            Verts.data(), Verts.size(), sizeof(Vertex));

        std::vector<Vertex>   RemappedVerts(UniqueCount);
        std::vector<uint32_t> RemappedIdx(Idx.size());
        meshopt_remapVertexBuffer(RemappedVerts.data(), Verts.data(),
                                  Verts.size(), sizeof(Vertex), Remap.data());
        meshopt_remapIndexBuffer(RemappedIdx.data(), Idx.data(),
                                 Idx.size(), Remap.data());
        Verts.swap(RemappedVerts);
        Idx.swap(RemappedIdx);

        // Vertex cache (T&L) optimization — reorders indices so the GPU
        // post-T&L cache hits more often.
        meshopt_optimizeVertexCache(Idx.data(), Idx.data(), Idx.size(), Verts.size());

        // Overdraw optimization — reorders triangles within cache-equivalent
        // bounds to reduce pixel shader overdraw. 1.05 = up to 5% cache regression.
        meshopt_optimizeOverdraw(
            Idx.data(), Idx.data(), Idx.size(),
            &Verts[0].Pos[0], Verts.size(), sizeof(Vertex),
            /*threshold=*/1.05f);

        // Vertex fetch optimization — reorders the VERTEX buffer to match
        // index access order so the pre-T&L cache hits more.
        meshopt_optimizeVertexFetch(Verts.data(), Idx.data(), Idx.size(),
                                    Verts.data(), Verts.size(), sizeof(Vertex));
        Optimized = true;
    }

    const uint32_t VtxCount = static_cast<uint32_t>(Verts.size());
    const uint32_t IdxCount = static_cast<uint32_t>(Idx.size());

    // ---- Pick index format -----------------------------------------------
    const bool UseU16 = VtxCount < 65536;
    const IndexFormat IdxFormat = UseU16 ? IndexFormat::U16 : IndexFormat::U32;

    // ---- Build name strings ----------------------------------------------
    char VtxName[96], IdxName[96];
    std::snprintf(VtxName, sizeof(VtxName), "%s.Verts",
                  Desc.DebugName ? Desc.DebugName : "Mesh");
    std::snprintf(IdxName, sizeof(IdxName), "%s.Indices",
                  Desc.DebugName ? Desc.DebugName : "Mesh");

    // ---- Upload vertex buffer (DeviceLocal, bindless storage) ------------
    rhi::BufferHandle VBuf = m_dev->CreateBuffer({
        .Size            = uint64_t(VtxCount) * sizeof(Vertex),
        .Usage           = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
        .Memory          = rhi::MemoryUsage::DeviceLocal,
        .DebugName       = VtxName,
        .InitialData     = Verts.data(),
        .InitialDataSize = uint64_t(VtxCount) * sizeof(Vertex),
    });
    HELIO_CHECK(VBuf.IsValid() && VBuf.BindlessSlot != 0xFFFFFFFFu);

    // ---- Pack + upload index buffer --------------------------------------
    rhi::BufferHandle IBuf{};
    if (UseU16) {
        std::vector<uint16_t> Packed(IdxCount);
        for (uint32_t I = 0; I < IdxCount; ++I) Packed[I] = static_cast<uint16_t>(Idx[I]);
        IBuf = m_dev->CreateBuffer({
            .Size            = uint64_t(IdxCount) * sizeof(uint16_t),
            .Usage           = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
            .Memory          = rhi::MemoryUsage::DeviceLocal,
            .DebugName       = IdxName,
            .InitialData     = Packed.data(),
            .InitialDataSize = uint64_t(IdxCount) * sizeof(uint16_t),
        });
    } else {
        IBuf = m_dev->CreateBuffer({
            .Size            = uint64_t(IdxCount) * sizeof(uint32_t),
            .Usage           = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst,
            .Memory          = rhi::MemoryUsage::DeviceLocal,
            .DebugName       = IdxName,
            .InitialData     = Idx.data(),
            .InitialDataSize = uint64_t(IdxCount) * sizeof(uint32_t),
        });
    }
    HELIO_CHECK(IBuf.IsValid());

    // ---- Assemble handle --------------------------------------------------
    Mesh M{};
    M.Id           = m_nextId++;
    M.VertexBuffer = VBuf;
    M.IndexBuffer  = IBuf;
    M.VertexCount  = VtxCount;
    M.IndexCount   = IdxCount;
    M.Indices      = IdxFormat;
    M.Bounds       = Src.Bounds;
    if (!M.Bounds.IsValid()) {
        // Bounds weren't set on the input (default AABB is the empty,
        // Min > Max state); compute from working copy.
        MeshData Tmp{};
        Tmp.Vertices = Verts;
        Tmp.RecomputeBounds();
        M.Bounds = Tmp.Bounds;
    }
    M.Stats.VertexCount   = VtxCount;
    M.Stats.TriangleCount = IdxCount / 3;
    M.Stats.Optimized     = Optimized;
    M.Stats.BuildMs       = float(BuildClock.SecondsSinceStart() * 1000.0);

    m_meshes[M.Id] = M;

    if (Desc.BuildBLAS) {
        HELIO_LOG_WARN("Resource",
            "MeshDesc::BuildBLAS=true on '{}' but auto-BLAS hook is not wired "
            "in V1 (Mesh struct reserves no BLAS field). Build manually via "
            "Device::BuildBLAS when needed.",
            Desc.DebugName ? Desc.DebugName : "<unnamed>");
    }

    HELIO_LOG_INFO("Resource",
        "Mesh '{}' built: {} verts, {} tris, {} indices ({}), {:.2f} ms{}",
        Desc.DebugName ? Desc.DebugName : "<unnamed>",
        VtxCount, M.Stats.TriangleCount, IdxCount,
        UseU16 ? "U16" : "U32", M.Stats.BuildMs,
        Optimized ? " (meshopt)" : "");

    return M;
}

void MeshSystem::DestroyMesh(Mesh M) {
    if (!M.IsValid()) return;
    auto It = m_meshes.find(M.Id);
    if (It == m_meshes.end()) return;
    if (It->second.VertexBuffer.IsValid()) m_dev->DestroyBuffer(It->second.VertexBuffer);
    if (It->second.IndexBuffer .IsValid()) m_dev->DestroyBuffer(It->second.IndexBuffer);
    m_meshes.erase(It);
}

} // namespace helio::resource
