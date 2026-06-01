#include "Math.h"

#include <cmath>

namespace helio {

// Packed types pulled into namespace scope below for clean impl access.

namespace math {

// ---------------------------------------------------------------------------
// CONVENTION: all matrices here are written in **standard column-vector form**
// (math/HLSL textbook layout). Applied as `mul(M, v_col)` in shaders or
// `hlslpp::mul(M, v_col)` on CPU.
//
// Translation lives in column 3 (entries M[0][3], M[1][3], M[2][3]).
// Composition order: `mul(Proj, View) * v_world` = clip coords. Read left-
// to-right as "apply the rightmost first."
// ---------------------------------------------------------------------------

float4x4 LookAtLH(float3 Eye, float3 Target, float3 Up) {
    float3 ZAxis = hlslpp::normalize(Target - Eye);
    float3 XAxis = hlslpp::normalize(hlslpp::cross(Up, ZAxis));
    float3 YAxis = hlslpp::cross(ZAxis, XAxis);

    // Standard column-vector form: basis vectors in ROWS, translation in
    // column 3. Then `mul(M, v_col)` gives (XAxis·(v-E), YAxis·(v-E),
    // ZAxis·(v-E), 1) — exactly the view-space coordinates.
    return float4x4(
        XAxis.x, XAxis.y, XAxis.z, -hlslpp::dot(XAxis, Eye),
        YAxis.x, YAxis.y, YAxis.z, -hlslpp::dot(YAxis, Eye),
        ZAxis.x, ZAxis.y, ZAxis.z, -hlslpp::dot(ZAxis, Eye),
        0.0f,    0.0f,    0.0f,     1.0f
    );
}

float4x4 PerspectiveReverseZLH(float FovYRadians, float Aspect, float NearZ) {
    // Reverse-Z infinite-far perspective in standard column-vector form.
    //   x_clip = XScale * x_view
    //   y_clip = -YScale * y_view      (Y negated for Vulkan's Y-down clip space)
    //   z_clip = NearZ                 (constant, depth = NearZ / Vz after divide)
    //   w_clip = z_view
    //
    // Negating Y here gives us world-up = screen-up under Vulkan's native
    // Y-down framebuffer convention. The side effect is that it flips the
    // signed-area sign of every triangle in clip space, so pipelines using
    // this projection MUST be created with `FrontFace::Clockwise` — that
    // tells Vulkan to interpret what was math-CCW as front-facing again,
    // restoring `CullMode::Back` semantics.
    float YScale = 1.0f / std::tan(FovYRadians * 0.5f);
    float XScale = YScale / Aspect;

    return float4x4(
        XScale,  0.0f,    0.0f, 0.0f,
        0.0f,   -YScale,  0.0f, 0.0f,
        0.0f,    0.0f,    0.0f, NearZ,
        0.0f,    0.0f,    1.0f, 0.0f
    );
}

// ---- Transform builders ----------------------------------------------------

float4x4 Identity() {
    return float4x4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
}

float4x4 Translation(float3 T) {
    // Column-vector form: translation in column 3. Applied as M * v_col,
    // result.xyz = v.xyz + T.
    return float4x4(
        1, 0, 0, float(T.x),
        0, 1, 0, float(T.y),
        0, 0, 1, float(T.z),
        0, 0, 0, 1
    );
}

float4x4 RotationX(float Radians) {
    const float C = std::cos(Radians), S = std::sin(Radians);
    return float4x4(
        1, 0,  0, 0,
        0, C, -S, 0,
        0, S,  C, 0,
        0, 0,  0, 1
    );
}

float4x4 RotationY(float Radians) {
    const float C = std::cos(Radians), S = std::sin(Radians);
    return float4x4(
         C, 0, S, 0,
         0, 1, 0, 0,
        -S, 0, C, 0,
         0, 0, 0, 1
    );
}

float4x4 RotationZ(float Radians) {
    const float C = std::cos(Radians), S = std::sin(Radians);
    return float4x4(
        C, -S, 0, 0,
        S,  C, 0, 0,
        0,  0, 1, 0,
        0,  0, 0, 1
    );
}

float4x4 Scale(float3 S) {
    return float4x4(
        float(S.x), 0,          0,          0,
        0,          float(S.y), 0,          0,
        0,          0,          float(S.z), 0,
        0,          0,          0,          1
    );
}

float4x4 TRS(float3 T, const float4x4& R, float3 S) {
    // Apply Scale, then Rotate, then Translate (read right-to-left in column form).
    return hlslpp::mul(Translation(T), hlslpp::mul(R, Scale(S)));
}

// ---- AABB ------------------------------------------------------------------

float3 AABB::Center() const { return (Min + Max) * 0.5f; }
float3 AABB::Extents() const { return (Max - Min) * 0.5f; }

bool AABB::Contains(float3 P) const {
    return hlslpp::all(P >= Min) && hlslpp::all(P <= Max);
}

void AABB::Expand(float3 P) {
    Min = hlslpp::min(Min, P);
    Max = hlslpp::max(Max, P);
}

void AABB::Expand(const AABB& Other) {
    Min = hlslpp::min(Min, Other.Min);
    Max = hlslpp::max(Max, Other.Max);
}

} // namespace math
} // namespace helio
