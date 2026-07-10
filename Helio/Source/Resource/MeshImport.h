/// @file MeshImport.h
/// @brief glTF 2.0 / GLB mesh import into engine `MeshData` (via fastgltf).
///
/// glTF is the runtime interchange format for Helio — author in a DCC tool
/// (Blender, Maya, …), export `.gltf` / `.glb`, and load it here. Each glTF
/// mesh primitive becomes one `MeshData` (CPU authored form) ready for
/// `MeshSystem::CreateMesh`.
///
/// Coordinate conversion: glTF is right-handed with -Z forward; Helio is
/// left-handed with +Z forward. The importer negates Z on positions, normals,
/// and tangents and reverses triangle winding, so an imported model faces the
/// engine's +Z and its front faces match the `FrontFace::Clockwise` +
/// `Cull::Back` convention every other mesh uses. Node transforms are NOT
/// baked in v1 — each primitive is imported in its local space and you place
/// it with the owning actor's transform.
#pragma once

#include "Material.h"
#include "MeshData.h"

#include <Core/Math/Math.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace helio::resource {

class TextureCache;

/// One imported primitive: its glTF mesh name (for the editor / debug names),
/// the CPU mesh data, and the material parsed from the primitive's glTF
/// material (factors always; texture slots when a `TextureCache` is supplied
/// to `ImportGltf`).
struct ImportedMesh {
    std::string Name;
    MeshData    Data;
    Material    Material;
    /// Index of the glTF *mesh* this primitive belongs to (into the asset's
    /// mesh array). Links a primitive back to the scene nodes that place it —
    /// several nodes may reference one mesh index, which is how instancing is
    /// expressed. Primitives of the same mesh share this value.
    std::size_t SourceMeshIndex{0};
};

/// One placement pulled from the glTF scene graph: which glTF mesh to draw and
/// where. `LocalToRoot` is the node's world transform *relative to the file
/// root*, already converted to Helio's left-handed space (the same Z-negation
/// the vertex data gets, applied to the transform). Multiple `ImportedNode`s
/// may share a `MeshIndex` — that is instancing, and the loader hands them the
/// same `Mesh` handle so they collapse into one instanced draw.
struct ImportedNode {
    std::string Name;
    std::size_t MeshIndex{0};
    float4x4    LocalToRoot;
};

/// The full result of importing a glTF/GLB: the geometry (one `ImportedMesh`
/// per primitive) plus the scene layout (`Nodes`). When `Nodes` is empty (a
/// file with no scene graph) the loader falls back to placing every primitive
/// at the origin.
struct ImportedScene {
    std::vector<ImportedMesh> Primitives;
    std::vector<ImportedNode> Nodes;
};

/// Load every triangle primitive from a `.gltf` or `.glb` file. Returns one
/// `ImportedMesh` per primitive (a glTF "mesh" can hold several). Missing
/// attributes are handled gracefully: absent normals are computed flat, absent
/// UVs default to (0,0); tangents are generated from UVs when a mesh has a
/// normal map but no `TANGENT`. Returns an empty vector on failure and logs
/// the reason (bad path, parse error, no triangle geometry).
///
/// When `Textures` is non-null, the material's images (base color, normal,
/// metallic-roughness, emissive, occlusion) are decoded and uploaded through
/// that cache, and their bindless slots are stored on each `Material`. Pass
/// null to import geometry + material FACTORS only (no GPU textures).
///
/// Also walks the glTF scene graph and returns one `ImportedNode` per placed
/// mesh (its world transform converted to Helio space), so a multi-object file
/// reconstructs its authored layout rather than piling every mesh at the
/// origin. Returns an empty scene on failure.
[[nodiscard]] ImportedScene ImportGltf(const std::filesystem::path& Path,
                                       TextureCache* Textures = nullptr);

} // namespace helio::resource
