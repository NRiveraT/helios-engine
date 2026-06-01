/// @file MeshPrimitives.h
/// @brief Procedural mesh data generators — Cube, Sphere, Plane, Cylinder.
///
/// Each function returns a fully-populated `MeshData` ready to hand to
/// `MeshSystem::CreateMesh`. Normals point outward, UVs are conventional
/// (cube: cubemap-like per-face mapping; sphere: lat/long; plane: 0..1
/// across the surface). Tangents are generated where straightforward;
/// otherwise zero-initialized (the shader's normal-mapping path falls back
/// to vertex normal when tangent is zero).
///
/// Centered at origin unless noted. Sizes are *full extents* (cube of size
/// 1 has corners at ±0.5).
#pragma once

#include "MeshData.h"

#include <cstdint>

namespace helio::resource::primitives {

/// Axis-aligned cube of `Size` x `Size` x `Size`, centered at origin.
/// 24 vertices (4 per face, hard-normals between faces), 36 indices.
[[nodiscard]] MeshData Cube(float Size = 1.0f);

/// UV sphere with `Segments` longitude divisions × `Rings` latitude divisions.
/// `Segments * (Rings + 1)` vertices, `Segments * Rings * 6` indices.
[[nodiscard]] MeshData Sphere(float Radius = 0.5f,
                              uint32_t Segments = 32,
                              uint32_t Rings    = 16);

/// XZ-plane (normal up +Y), centered at origin. `SubdivX` x `SubdivZ`
/// quads — each quad = 4 verts, 6 indices. `Width` x `Depth` extents.
[[nodiscard]] MeshData Plane(float Width = 1.0f, float Depth = 1.0f,
                             uint32_t SubdivX = 1, uint32_t SubdivZ = 1);

/// Y-axis-aligned cylinder of `Radius` and `Height`, centered at origin.
/// `Segments` around. Includes top and bottom caps.
[[nodiscard]] MeshData Cylinder(float Radius = 0.5f, float Height = 1.0f,
                                uint32_t Segments = 24);

} // namespace helio::resource::primitives
