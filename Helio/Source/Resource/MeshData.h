/// @file MeshData.h
/// @brief CPU-side mesh authoring types — `Vertex`, `MeshData`, `IndexFormat`.
///
/// `MeshData` is the in-memory authored form. You build one by hand, via
/// `MeshPrimitives::Cube()`-style helpers, or via `MeshLoader::LoadGltf*`,
/// then hand it to `MeshSystem::CreateMesh` to upload + optimize + register.
///
/// **About the Vertex layout**: hlslpp's `float3` is SIMD-aligned (16 B), so
/// it's not suitable for vertex layouts that need 12-B-packed positions. We
/// use raw `float[N]` arrays for vertex attributes so the on-disk / GPU layout
/// is exactly 48 B (matching `Helio/Shaders/Common/Vertex.slang`). The
/// `Get*` / `Set*` helpers convert to/from hlslpp::float3 for math.
///
/// Tangent.W carries the bitangent sign (glTF convention), so the shader can
/// reconstruct bitangent as `cross(Normal, Tangent.xyz) * Tangent.w` without
/// a separate buffer.
#pragma once

#include <Core/Math/Math.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace helio::resource {

/// 48-byte packed vertex matching Shaders/Common/Vertex.slang byte-for-byte.
struct Vertex {
    float Pos[3];      // 12
    float Normal[3];   // 12
    float UV[2];       //  8
    float Tangent[4];  // 16 — xyz = tangent, w = bitangent sign (+1 / -1)
};
static_assert(sizeof(Vertex) == 48, "Vertex must match Shaders/Common/Vertex.slang");

// ---- math interop helpers (raw array <-> hlslpp::floatN) -------------------
[[nodiscard]] inline float3 GetPos(const Vertex& V)     { return float3(V.Pos[0], V.Pos[1], V.Pos[2]); }
[[nodiscard]] inline float3 GetNormal(const Vertex& V)  { return float3(V.Normal[0], V.Normal[1], V.Normal[2]); }
[[nodiscard]] inline float2 GetUV(const Vertex& V)      { return float2(V.UV[0], V.UV[1]); }
[[nodiscard]] inline float4 GetTangent(const Vertex& V) { return float4(V.Tangent[0], V.Tangent[1], V.Tangent[2], V.Tangent[3]); }

inline void SetPos(Vertex& V, float3 P) {
    V.Pos[0] = float(P.x); V.Pos[1] = float(P.y); V.Pos[2] = float(P.z);
}
inline void SetNormal(Vertex& V, float3 N) {
    V.Normal[0] = float(N.x); V.Normal[1] = float(N.y); V.Normal[2] = float(N.z);
}
inline void SetUV(Vertex& V, float2 U) {
    V.UV[0] = float(U.x); V.UV[1] = float(U.y);
}
inline void SetTangent(Vertex& V, float4 T) {
    V.Tangent[0] = float(T.x); V.Tangent[1] = float(T.y);
    V.Tangent[2] = float(T.z); V.Tangent[3] = float(T.w);
}

enum class IndexFormat : uint8_t { U16, U32 };

/// CPU-side authored mesh. Indices are always uint32 in memory; the GPU-side
/// `Mesh` may convert to U16 if vertex count permits (saves 50% index VRAM).
struct MeshData {
    std::vector<Vertex>   Vertices;
    std::vector<uint32_t> Indices;
    math::AABB            Bounds{};   ///< Computed by `RecomputeBounds()` or set by the source.

    /// Rebuild `Bounds` from `Vertices`. Call after mutating vertices.
    void RecomputeBounds();

    /// True if Vertices.size() < 65536 — `MeshSystem::CreateMesh` will pack
    /// indices as U16 if so (transparent to shaders via `BindIndexBuffer`).
    [[nodiscard]] bool FitsU16Indices() const noexcept {
        return Vertices.size() < 65536;
    }
};

} // namespace helio::resource
