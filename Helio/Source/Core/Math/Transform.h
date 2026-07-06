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
/// composition / interpolation / matrix conversion.
///
/// All operations follow the same column-vector convention as the rest of
/// `Core/Math/Math.h` — `Transform::ToMatrix()` returns a matrix `M` such
/// that `mul(M, v_col)` produces `Scale, then Rotate, then Translate`.
///
/// Non-uniform scale caveat (same limitation as UE's `FTransform`): a
/// `Transform` cannot represent shear, so `operator*` and `Inverse()` are
/// exact only when scale is uniform (or when rotations are axis-aligned with
/// the scaling). `TransformPoint` / `InverseTransformPoint` are always exact.
#pragma once

#include "Math.h"

namespace helio {

struct Transform {
    float3 Position{0.0f, 0.0f, 0.0f};
    /// Quaternion (xyz, w). Identity = (0, 0, 0, 1). Kept unit-length by
    /// every mutator in this file.
    float4 Rotation{0.0f, 0.0f, 0.0f, 1.0f};
    float3 Scale{1.0f, 1.0f, 1.0f};

    Transform() noexcept = default;
    explicit Transform(float3 InPosition) noexcept : Position(InPosition) {}
    Transform(float3 InPosition, float4 InRotation) noexcept
        : Position(InPosition), Rotation(InRotation) {}
    Transform(float3 InPosition, float4 InRotation, float3 InScale) noexcept
        : Position(InPosition), Rotation(InRotation), Scale(InScale) {}

    /// Compose into a 4x4 column-vector matrix: T * R * S.
    [[nodiscard]] float4x4 ToMatrix() const;

    /// View matrix for a camera whose world placement is this transform:
    /// the exact rigid inverse `[R^T | -R^T·P]`. Scale is intentionally
    /// ignored — a camera's basis must stay orthonormal.
    [[nodiscard]] float4x4 ToViewMatrix() const;

    /// Rigid+scale inverse: `this * Inverse() ≈ identity`. Exact for uniform
    /// scale (see file-header caveat on non-uniform scale).
    [[nodiscard]] Transform Inverse() const;

    /// Compose two transforms. `parent * child` applied to a point gives the
    /// same result as `parent.ToMatrix() * child.ToMatrix() * v_col`.
    [[nodiscard]] Transform operator*(const Transform& Child) const;

    // ---- Point / vector maps (always exact) --------------------------------

    /// Local point -> world: `Position + Rotation * (Scale * P)`.
    [[nodiscard]] float3 TransformPoint(float3 P) const;
    /// World point -> local: `Scale⁻¹ * (Rotation⁻¹ * (P - Position))`.
    [[nodiscard]] float3 InverseTransformPoint(float3 P) const;
    /// Local direction -> world, scaled, not translated.
    [[nodiscard]] float3 TransformVector(float3 V) const;
    /// World direction -> local, inverse-scaled, not translated.
    [[nodiscard]] float3 InverseTransformVector(float3 V) const;

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

    /// Pre-multiply rotation by `Q`: adds `Q`'s rotation on top of the
    /// existing one, about WORLD axes.
    void Rotate(float4 Q);

    /// Post-multiply rotation by `Q`: adds `Q`'s rotation about the
    /// transform's own LOCAL axes.
    void RotateLocal(float4 Q);

    /// Equivalent to `Rotate(QuatFromAxisAngle(Axis, Radians))`.
    void RotateAxis(float3 Axis, float Radians);

    /// Equivalent to `Rotate(QuatFromEuler(Pitch, Yaw, Roll))` — intrinsic
    /// Yaw → Pitch → Roll (see `QuatFromEuler`).
    void RotateEuler(float PitchRadians, float YawRadians, float RollRadians);

    void ScaleBy(float3 S) noexcept;
    inline void ScaleBy(float Uniform) noexcept {
        ScaleBy(float3(Uniform, Uniform, Uniform));
    }
};

// =============================================================================
// Quaternion helpers (free functions, also useful outside Transform)
//
// Convention notes that MUST hold everywhere (locked by Tests/MathTests.cpp):
// - Hamilton product, `QuatMul(A, B)` applies B first, then A — matching
//   column-vector matrix composition `mul(Ma, Mb)`.
// - Euler angles are intrinsic Tait-Bryan Yaw(Y) → Pitch(X) → Roll(Z),
//   composed as `qYaw * qPitch * qRoll` (Unity / UE camera convention).
// =============================================================================

[[nodiscard]] inline float4 QuatIdentity() noexcept { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

[[nodiscard]] float4 QuatFromAxisAngle(float3 Axis, float Radians);

/// Intrinsic Tait-Bryan: rotation about world Y (yaw), then about the new
/// local X (pitch), then about the new local Z (roll). Algebraically equal to
/// `QuatMul(QuatMul(QuatFromAxisAngle(Y, Yaw), QuatFromAxisAngle(X, Pitch)),
///          QuatFromAxisAngle(Z, Roll))`.
[[nodiscard]] float4 QuatFromEuler(float PitchRadians, float YawRadians, float RollRadians);

/// Inverse of `QuatFromEuler`: extract (pitch, yaw, roll) such that
/// `QuatFromEuler(result)` reproduces the same rotation. Pitch is returned in
/// `[-π/2, π/2]`; at the gimbal-lock poles (|pitch| = π/2) roll is defined
/// as 0 and yaw absorbs the remaining rotation.
[[nodiscard]] float3 QuatToEuler(float4 Q);

/// Standard Hamilton product. Applies `B` first, then `A`, when used to
/// rotate a vector via `qmul(qmul(A, vAsQuat), conj(A))`.
[[nodiscard]] float4 QuatMul(float4 A, float4 B);

[[nodiscard]] float4 QuatNormalize(float4 Q);

[[nodiscard]] float4 QuatConjugate(float4 Q);

/// Inverse of a UNIT quaternion (== conjugate). All quaternions produced by
/// this library are unit-length; renormalize first if yours may not be.
[[nodiscard]] inline float4 QuatInverse(float4 Q) { return QuatConjugate(Q); }

[[nodiscard]] float QuatDot(float4 A, float4 B);

/// Rotate `V` by quaternion `Q` (i.e. `q * v * q^-1`). Length-preserving —
/// rotating a non-unit vector scales nothing. The kernel behind
/// `Transform::GetForward/Right/Up` and `Transform::RotateVector`. Mirrors
/// `FQuat::RotateVector` in Unreal — ~15 flops, faster than building the
/// rotation matrix and multiplying.
[[nodiscard]] float3 QuatRotateVector(float4 Q, float3 V);

/// Rotate `V` by the inverse of `Q` (i.e. into the local frame). Mirrors
/// `FQuat::UnrotateVector`.
[[nodiscard]] float3 QuatUnrotateVector(float4 Q, float3 V);

/// Spherical linear interpolation. `T` in `[0, 1]`. Robust against opposite-
/// signed quaternions (picks the shortest arc).
[[nodiscard]] float4 QuatSlerp(float4 A, float4 B, float T);

/// Normalized linear interpolation — the cheap workhorse for small angular
/// deltas (per-frame blending, physics state interpolation). Shortest-arc
/// like `QuatSlerp`, non-constant angular velocity.
[[nodiscard]] float4 QuatNlerp(float4 A, float4 B, float T);

/// Rotation matrix (4x4, rotation in the upper 3x3) from a unit quaternion.
[[nodiscard]] float4x4 QuatToMatrix(float4 Q);

/// Unit quaternion from the rotation part (upper 3x3) of a column-vector
/// matrix. The 3x3 must be a pure rotation — orthonormalize first if it may
/// carry scale. Shepperd's method: numerically stable near 180° rotations.
[[nodiscard]] float4 QuatFromMatrix(const float4x4& M);

/// Rotation that aims local +Z at `Forward` with local +Y as close to `Up`
/// as possible (LH). Degenerate inputs (zero forward, up parallel to
/// forward) fall back the same way `math::LookAtLH` does. Mirrors Unity's
/// `Quaternion.LookRotation`.
[[nodiscard]] float4 QuatLookRotation(float3 Forward, float3 Up = float3(0.0f, 1.0f, 0.0f));

} // namespace helio
