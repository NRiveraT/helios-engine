/// @file Math.h
/// @brief hlslpp re-exports + engine-specific helpers.
///
/// All vector and matrix types use HLSL-style names (float3, float4x4)
/// matching the Slang shader code 1:1. This keeps CPU and GPU math
/// visually identical.
///
/// Coordinate conventions:
/// - Left-handed (+X right, +Y up, +Z forward)
/// - Reverse-Z perspective (near=1, far=0) for best depth precision on Vulkan
#pragma once

#include <hlslpp/hlsl++.h>

namespace helio {

using hlslpp::float1;
using hlslpp::float2;
using hlslpp::float3;
using hlslpp::float4;
using hlslpp::int1;
using hlslpp::int2;
using hlslpp::int3;
using hlslpp::int4;
using hlslpp::uint1;
using hlslpp::uint2;
using hlslpp::uint3;
using hlslpp::uint4;
using hlslpp::float2x2;
using hlslpp::float3x3;
using hlslpp::float4x4;

namespace math {

/// Left-handed look-at view matrix.
[[nodiscard]] float4x4 LookAtLH(float3 Eye, float3 Target, float3 Up);

/// Reverse-Z infinite-far perspective. Depth maps near=1 -> +inf=0.
/// Pass NearZ small (e.g. 0.01f) to keep precision at distance.
[[nodiscard]] float4x4 PerspectiveReverseZLH(float FovYRadians, float Aspect, float NearZ);

// ---- Transform builders ---------------------------------------------------

[[nodiscard]] float4x4 Identity();
[[nodiscard]] float4x4 Translation(float3 T);
[[nodiscard]] inline float4x4 Translation(float X, float Y, float Z) {
    return Translation(float3(X, Y, Z));
}
[[nodiscard]] float4x4 RotationX(float Radians);
[[nodiscard]] float4x4 RotationY(float Radians);
[[nodiscard]] float4x4 RotationZ(float Radians);
[[nodiscard]] float4x4 Scale(float3 S);
[[nodiscard]] inline float4x4 Scale(float Uniform) { return Scale(float3(Uniform, Uniform, Uniform)); }

/// Compose: returns `Translate * Rotate * Scale` — the standard SRT order.
[[nodiscard]] float4x4 TRS(float3 T, const float4x4& R, float3 S);

/// Axis-aligned bounding box.
struct AABB {
    float3 Min;
    float3 Max;

    [[nodiscard]] float3 Center() const;
    [[nodiscard]] float3 Extents() const;
    [[nodiscard]] bool Contains(float3 P) const;
    void Expand(float3 P);
    void Expand(const AABB& Other);
};

} // namespace math

// =============================================================================
// Packed GPU-layout types
//
// hlslpp::floatN are SIMD-aligned (16 B regardless of N), which is great for
// math but wrong for vertex/push-constant layouts where the shader expects
// 12 B for float3, 8 B for float2, etc. Use these `*Packed` wrappers in
// push-constant structs and CPU-side vertex buffers — they have assignment
// operators from the matching hlslpp type so usage stays clean:
//
//     PC.LightDir = float3(0, -1, 0);   // assigns through operator=(float3)
//     PC.ViewProj = ViewProj;           // assigns through operator=(float4x4),
//                                       // handling the row→column transpose
//                                       // the SPIR-V backend expects.
// =============================================================================

struct Vec2Packed {
    float V[2];
    Vec2Packed& operator=(float2 X) noexcept {
        V[0] = float(X.x); V[1] = float(X.y);
        return *this;
    }
    [[nodiscard]] float2 ToFloat2() const noexcept { return float2(V[0], V[1]); }
};
static_assert(sizeof(Vec2Packed) == 8);

struct Vec3Packed {
    float V[3];
    Vec3Packed& operator=(float3 X) noexcept {
        V[0] = float(X.x); V[1] = float(X.y); V[2] = float(X.z);
        return *this;
    }
    [[nodiscard]] float3 ToFloat3() const noexcept { return float3(V[0], V[1], V[2]); }
};
static_assert(sizeof(Vec3Packed) == 12);

struct Vec4Packed {
    float V[4];
    Vec4Packed& operator=(float4 X) noexcept {
        V[0] = float(X.x); V[1] = float(X.y); V[2] = float(X.z); V[3] = float(X.w);
        return *this;
    }
    [[nodiscard]] float4 ToFloat4() const noexcept { return float4(V[0], V[1], V[2], V[3]); }
};
static_assert(sizeof(Vec4Packed) == 16);

/// Row-major 4x4 matrix in 64 packed bytes, suitable for direct upload to
/// shaders that expect `mul(M, v)` semantics. Assignment from `float4x4`
/// transposes from hlslpp's column-major storage so Slang's column-major
/// SPIR-V default reads it correctly.
struct Mat4Packed {
    float V[16];
    Mat4Packed& operator=(const float4x4& M) noexcept {
        hlslpp::store_transposed(V, M);
        return *this;
    }
};
static_assert(sizeof(Mat4Packed) == 64);

} // namespace helio
