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
[[nodiscard]] std::vector<ImportedMesh> ImportGltf(const std::filesystem::path& Path,
                                                   TextureCache* Textures = nullptr);

} // namespace helio::resource
