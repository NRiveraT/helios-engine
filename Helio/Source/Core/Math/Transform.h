/// @file Transform.h
/// @brief Position + quaternion + scale, the canonical mutable transform type.
///
/// `Transform` is the high-level representation game code holds onto. It
/// decouples *what* an object is (position / rotation / scale, all individually
/// mutable) from *how the GPU consumes it* (`MeshInstance`, `Mat4Packed`,
/// pushed-buffer slot indices). Convert to a `float4x4` at the last moment via
/// `ToMatrix()`, or pass a `Transform` directly to `InstanceBatch::Add` which
/// handles the conversion for you.
///
/// Rotation is a quaternion stored as `float4` (`xyz, w`). Identity is
/// `(0, 0, 0, 1)`. Quaternion helpers are below for axis-angle / Euler /
/// composition / interpolation.
///
/// All operations follow the same column-vector convention as the rest of
/// `Core/Math/Math.h` — `Transform::ToMatrix()` returns a matrix `M` such
/// that `mul(M, v_col)` produces `Scale, then Rotate, then Translate`.
#pragma once

#include "Math.h"

namespace helio {

struct Transform {
    float3 Position{0.0f, 0.0f, 0.0f};
    /// Quaternion (xyz, w). Identity = (0, 0, 0, 1).
    float4 Rotation{0.0f, 0.0f, 0.0f, 1.0f};
    float3 Scale{1.0f, 1.0f, 1.0f};

    Transform() noexcept :
    Position(0.f),
    Rotation(0.f, 0.f, 0.f, 1.f),
    Scale(1.f)
    {}
    
    /// Compose into a 4x4 column-vector matrix: T * R * S.
    [[nodiscard]] float4x4 ToMatrix() const;

    /// Compose two transforms. `parent * child` applied to a point gives the
    /// same result as `parent.ToMatrix() * child.ToMatrix() * v_col`.
    [[nodiscard]] Transform operator*(const Transform& Child) const;

    // ---- Basis axes (world-space) ------------------------------------------
    //
    // Convention: +X right, +Y up, +Z forward (left-handed). Matches the LH
    // perspective / look-at builders in `Math.h`. These mirror UE's
    // `FQuat::GetAxisX/Y/Z` — the rotation is applied to the canonical basis
    // vector via `QuatRotateVector`, avoiding the full 4x4 matrix build.

    /// Local +Z (forward) axis expressed in world space.
    [[nodiscard]] float3 GetForward() const noexcept;
    /// Local +X (right) axis expressed in world space.
    [[nodiscard]] float3 GetRight() const noexcept;
    /// Local +Y (up) axis expressed in world space.
    [[nodiscard]] float3 GetUp() const noexcept;

    /// Rotate a local-space direction into world space (mirrors
    /// `FTransform::TransformVectorNoScale` / `FQuat::RotateVector`).
    [[nodiscard]] float3 RotateVector(float3 LocalDir) const noexcept;
    /// Inverse: rotate a world-space direction back into the local frame.
    [[nodiscard]] float3 UnrotateVector(float3 WorldDir) const noexcept;

    // ---- In-place mutators (cheaper than building matrices) ----------------

    void Translate(float3 Delta) noexcept;
    inline void Translate(float X, float Y, float Z) noexcept {
        Translate(float3(X, Y, Z));
    }

    /// Pre-multiply rotation by `Q` (i.e. add `Q`'s rotation on top of the
    /// existing one in world space).
    void Rotate(float4 Q);

    /// Equivalent to `Rotate(QuatFromAxisAngle(Axis, Radians))`.
    void RotateAxis(float3 Axis, float Radians);

    /// Intrinsic Tait-Bryan, applied as Yaw → Pitch → Roll.
    void RotateEuler(float PitchRadians, float YawRadians, float RollRadians);

    void ScaleBy(float3 S) noexcept;
    inline void ScaleBy(float Uniform) noexcept {
        ScaleBy(float3(Uniform, Uniform, Uniform));
    }
};

// =============================================================================
// Quaternion helpers (free functions, also useful outside Transform)
// =============================================================================

[[nodiscard]] inline float4 QuatIdentity() noexcept { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

[[nodiscard]] float4 QuatFromAxisAngle(float3 Axis, float Radians);

/// Intrinsic Tait-Bryan: rotation about world Y (yaw), then about the new
/// local X (pitch), then about the new local Z (roll). Equivalent to the
/// composition `Yaw * Pitch * Roll` applied to a column vector.
[[nodiscard]] float4 QuatFromEuler(float PitchRadians, float YawRadians, float RollRadians);

/// Standard Hamilton product. Applies `B` first, then `A`, when used to
/// rotate a vector via `qmul(qmul(A, vAsQuat), conj(A))`.
[[nodiscard]] float4 QuatMul(float4 A, float4 B);

[[nodiscard]] float4 QuatNormalize(float4 Q);

[[nodiscard]] float4 QuatConjugate(float4 Q);

/// Rotate `V` by quaternion `Q` (i.e. `q * v * q^-1`). The kernel behind
/// `Transform::GetForward/Right/Up` and `Transform::RotateVector`. Mirrors
/// `FQuat::RotateVector` in Unreal — ~12 mul, faster than building the
/// rotation matrix and multiplying.
[[nodiscard]] float3 QuatRotateVector(float4 Q, float3 V);

/// Rotate `V` by the inverse of `Q` (i.e. into the local frame). Mirrors
/// `FQuat::UnrotateVector`.
[[nodiscard]] float3 QuatUnrotateVector(float4 Q, float3 V);

/// Spherical linear interpolation. `T` in `[0, 1]`. Robust against opposite-
/// signed quaternions (picks the shortest arc).
[[nodiscard]] float4 QuatSlerp(float4 A, float4 B, float T);

/// 3x3-only rotation matrix from a quaternion. Useful for transforming
/// normals without translation overhead.
[[nodiscard]] float4x4 QuatToMatrix(float4 Q);

} // namespace helio
