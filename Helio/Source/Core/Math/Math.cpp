#include "Math.h"

#include <cmath>

namespace helio {
namespace math {

// ---------------------------------------------------------------------------
// CONVENTION: all matrices here are written in **standard column-vector form**
// (math/HLSL textbook layout). Applied as `mul(M, v_col)` in shaders or
// `hlslpp::mul(M, v_col)` on CPU.
//
// Translation lives in column 3 (entries M[0][3], M[1][3], M[2][3]).
// Composition order: `mul(Proj, View) * v_world` = clip coords. Read left-
// to-right as "apply the rightmost first."
//
// hlslpp stores float4x4 row-major, and the float4x4(...) constructor takes
// the first four scalars as ROW 0 — so the layouts below read exactly like
// the textbook matrices they implement.
// ---------------------------------------------------------------------------

float4x4 LookAtLH(float3 Eye, float3 Target, float3 Up) {
    constexpr float kEpsSq = 1e-12f;

    float3 ZAxis = Target - Eye;
    if (float(hlslpp::dot(ZAxis, ZAxis)) < kEpsSq) {
        ZAxis = float3(0.0f, 0.0f, 1.0f); // Eye == Target: default to +Z forward.
    } else {
        ZAxis = hlslpp::normalize(ZAxis);
    }

    // Degenerate Up (parallel to the view direction) breaks the cross
    // product. Fall back to world +Y; if forward IS ±Y, fall back to +Z —
    // both keep the basis orthonormal and deterministic.
    float3 XAxis = hlslpp::cross(Up, ZAxis);
    if (float(hlslpp::dot(XAxis, XAxis)) < kEpsSq) {
        const float3 FallbackUp = std::abs(float(ZAxis.y)) > 0.999f
                                      ? float3(0.0f, 0.0f, 1.0f)
                                      : float3(0.0f, 1.0f, 0.0f);
        XAxis = hlslpp::cross(FallbackUp, ZAxis);
    }
    XAxis = hlslpp::normalize(XAxis);
    const float3 YAxis = hlslpp::cross(ZAxis, XAxis);

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

float4x4 OrthoOffCenterReverseZLH(
    float Left, float Right, float Bottom, float Top, float NearZ, float FarZ) {
    // Off-center LH orthographic, same clip-space conventions as the
    // perspective projection above:
    //   x_clip = 2/(R-L) x - (R+L)/(R-L)        maps [L, R]   -> [-1, +1]
    //   y_clip = -(2/(T-B) y - (T+B)/(T-B))     maps [B, T]   -> [+1, -1]  (Y negated)
    //   z_clip = (F - z)/(F - N)                maps [N, F]   -> [ 1,  0]  (reverse-Z)
    //   w_clip = 1
    //
    // Same Y negation, same reverse-Z, same winding consequence
    // (`FrontFace::Clockwise`) as PerspectiveReverseZLH — a shadow pass and
    // the main pass share every pipeline convention.
    const float InvW = 1.0f / (Right - Left);
    const float InvH = 1.0f / (Top - Bottom);
    const float InvDepth = 1.0f / (FarZ - NearZ);

    return float4x4(
        2.0f * InvW,  0.0f,          0.0f,     -(Right + Left) * InvW,
        0.0f,        -2.0f * InvH,   0.0f,      (Top + Bottom) * InvH,
        0.0f,         0.0f,         -InvDepth,  FarZ * InvDepth,
        0.0f,         0.0f,          0.0f,      1.0f
    );
}

float4x4 OrthoReverseZLH(float Width, float Height, float NearZ, float FarZ) {
    const float HalfW = 0.5f * Width;
    const float HalfH = 0.5f * Height;
    return OrthoOffCenterReverseZLH(-HalfW, HalfW, -HalfH, HalfH, NearZ, FarZ);
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

bool AABB::IsValid() const { return hlslpp::all(Min <= Max); }

float3 AABB::Center() const { return (Min + Max) * 0.5f; }
float3 AABB::Extents() const { return (Max - Min) * 0.5f; }

bool AABB::Contains(float3 P) const {
    return hlslpp::all(P >= Min) && hlslpp::all(P <= Max);
}

float3 AABB::Corner(int I) const {
    return float3(
        (I & 1) ? float(Max.x) : float(Min.x),
        (I & 2) ? float(Max.y) : float(Min.y),
        (I & 4) ? float(Max.z) : float(Min.z)
    );
}

void AABB::Expand(float3 P) {
    Min = hlslpp::min(Min, P);
    Max = hlslpp::max(Max, P);
}

void AABB::Expand(const AABB& Other) {
    Min = hlslpp::min(Min, Other.Min);
    Max = hlslpp::max(Max, Other.Max);
}

AABB AABB::Transformed(const float4x4& M) const {
    if (!IsValid()) return {};

    // Arvo's method. hlslpp::store writes the row-major register layout, so
    // element (row I, col J) of the column-vector matrix is E[I*4 + J] and
    // the translation is column 3 (E[I*4 + 3]).
    alignas(16) float E[16];
    hlslpp::store(E, M);
    const float Lo[3] = {float(Min.x), float(Min.y), float(Min.z)};
    const float Hi[3] = {float(Max.x), float(Max.y), float(Max.z)};

    float OutMin[3], OutMax[3];
    for (int I = 0; I < 3; ++I) {
        OutMin[I] = OutMax[I] = E[I * 4 + 3];
        for (int J = 0; J < 3; ++J) {
            const float A = E[I * 4 + J] * Lo[J];
            const float B = E[I * 4 + J] * Hi[J];
            OutMin[I] += A < B ? A : B;
            OutMax[I] += A < B ? B : A;
        }
    }

    AABB Out;
    Out.Min = float3(OutMin[0], OutMin[1], OutMin[2]);
    Out.Max = float3(OutMax[0], OutMax[1], OutMax[2]);
    return Out;
}

} // namespace math
} // namespace helio
