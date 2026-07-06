#include "Transform.h"

#include <algorithm>
#include <cmath>

namespace helio
{
    // =============================================================================
    // Quaternion helpers
    // =============================================================================
    float4 QuatFromAxisAngle(float3 Axis, float Radians)
    {
        // A zero-length axis has no meaningful rotation and would make
        // hlslpp::normalize divide by zero, NaN-poisoning the quaternion (and
        // every transform it touches). Guard it the same way LookAtLH and
        // QuatLookRotation guard their degenerate inputs.
        const float Len2 = float(hlslpp::dot(Axis, Axis));
        if (Len2 < 1e-12f)
        {
            return QuatIdentity();
        }
        const float Half = 0.5f * Radians;
        const float S = std::sin(Half);
        const float C = std::cos(Half);
        const float3 N = Axis * (1.0f / std::sqrt(Len2));
        return float4(float(N.x) * S, float(N.y) * S, float(N.z) * S, C);
    }

    float4 QuatFromEuler(float Pitch, float Yaw, float Roll)
    {
        // Intrinsic Tait-Bryan Yaw (Y) → Pitch (X) → Roll (Z), composed as
        // q = qYaw * qPitch * qRoll — the Unity / Unreal "FPS camera" order.
        // This is the algebraic expansion of that exact product; the identity
        // is pinned by Tests/MathTests.cpp against explicit QuatMul chains.
        const float HP = 0.5f * Pitch, HY = 0.5f * Yaw, HR = 0.5f * Roll;
        const float cp = std::cos(HP), sp = std::sin(HP);
        const float cy = std::cos(HY), sy = std::sin(HY);
        const float cr = std::cos(HR), sr = std::sin(HR);
        return QuatNormalize(float4(
            sp * cy * cr + cp * sy * sr,
            cp * sy * cr - sp * cy * sr,
            cp * cy * sr - sp * sy * cr,
            cp * cy * cr + sp * sy * sr
        ));
    }

    float3 QuatToEuler(float4 Q)
    {
        // Inverse of QuatFromEuler (M = Ry * Rx * Rz in column-vector form).
        // Using the quaternion-to-matrix element identities:
        //   M12 = 2(yz - wx) = -sin(pitch)
        //   M02 = 2(xz + wy),  M22 = 1 - 2(xx + yy)   → yaw  = atan2(M02, M22)
        //   M10 = 2(xy + wz),  M11 = 1 - 2(xx + zz)   → roll = atan2(M10, M11)
        const float x = float(Q.x), y = float(Q.y), z = float(Q.z), w = float(Q.w);

        const float SinPitch = std::clamp(2.0f * (w * x - y * z), -1.0f, 1.0f);
        const float Pitch = std::asin(SinPitch);

        // Gimbal lock: cos(pitch) ≈ 0, yaw and roll rotate about the same
        // world axis. Convention: roll = 0, yaw absorbs the full rotation.
        constexpr float kLockThreshold = 0.9999995f;
        if (std::abs(SinPitch) >= kLockThreshold)
        {
            // pitch = +π/2: M = [[cos(y−r), sin(y−r), ·] ...] → yaw−roll = atan2( M01', M00)
            // pitch = −π/2: M00 = cos(y+r), M01 = −sin(y+r)   → yaw+roll = atan2(−M01, M00)
            // With roll := 0 both reduce to the expressions below.
            const float M00 = 1.0f - 2.0f * (y * y + z * z);
            const float M01 = 2.0f * (x * y - w * z);
            const float Yaw = (SinPitch > 0.0f) ? std::atan2(M01, M00)
                                                : std::atan2(-M01, M00);
            return float3(Pitch, Yaw, 0.0f);
        }

        const float Yaw = std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y));
        const float Roll = std::atan2(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z));
        return float3(Pitch, Yaw, Roll);
    }

    float4 QuatMul(float4 A, float4 B)
    {
        const float ax = float(A.x), ay = float(A.y), az = float(A.z), aw = float(A.w);
        const float bx = float(B.x), by = float(B.y), bz = float(B.z), bw = float(B.w);
        return float4(
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz
        );
    }

    float4 QuatNormalize(float4 Q)
    {
        const float x = float(Q.x), y = float(Q.y), z = float(Q.z), w = float(Q.w);
        const float Len2 = x * x + y * y + z * z + w * w;
        if (Len2 <= 1e-12f) return QuatIdentity();
        const float Inv = 1.0f / std::sqrt(Len2);
        return float4(x * Inv, y * Inv, z * Inv, w * Inv);
    }

    float4 QuatConjugate(float4 Q)
    {
        return float4(-float(Q.x), -float(Q.y), -float(Q.z), float(Q.w));
    }

    float QuatDot(float4 A, float4 B)
    {
        return float(A.x) * float(B.x) + float(A.y) * float(B.y) +
               float(A.z) * float(B.z) + float(A.w) * float(B.w);
    }

    float3 QuatRotateVector(float4 Q, float3 V)
    {
        // v' = 2(u·v)u + (w² − u·u)v + 2w(u×v), where u = q.xyz, w = q.w.
        // Algebraic expansion of q*v*q^-1 — same identity Unreal's
        // `FQuat::RotateVector` uses. For unit Q this is an exact rotation:
        // it preserves |V|, so it must NOT renormalize (a rotated velocity,
        // scaled offset, or zero vector passes through unchanged in length).
        const float ux = float(Q.x), uy = float(Q.y), uz = float(Q.z);
        const float vx = float(V.x), vy = float(V.y), vz = float(V.z);
        const float w = float(Q.w);

        const float udotv = ux * vx + uy * vy + uz * vz;
        const float udotu = ux * ux + uy * uy + uz * uz;
        const float k1 = 2.0f * udotv;
        const float k2 = w * w - udotu;
        const float k3 = 2.0f * w;

        // u × v
        const float cx = uy * vz - uz * vy;
        const float cy = uz * vx - ux * vz;
        const float cz = ux * vy - uy * vx;

        return float3(
            k1 * ux + k2 * vx + k3 * cx,
            k1 * uy + k2 * vy + k3 * cy,
            k1 * uz + k2 * vz + k3 * cz);
    }

    float3 QuatUnrotateVector(float4 Q, float3 V)
    {
        return QuatRotateVector(QuatConjugate(Q), V);
    }

    float4 QuatSlerp(float4 A, float4 B, float T)
    {
        float ax = float(A.x), ay = float(A.y), az = float(A.z), aw = float(A.w);
        float bx = float(B.x), by = float(B.y), bz = float(B.z), bw = float(B.w);

        float Cos = ax * bx + ay * by + az * bz + aw * bw;
        // Pick shortest arc.
        if (Cos < 0.0f)
        {
            bx = -bx;
            by = -by;
            bz = -bz;
            bw = -bw;
            Cos = -Cos;
        }
        // Nearly-parallel — linear interpolate to avoid divide-by-near-zero.
        if (Cos > 0.9995f)
        {
            return QuatNormalize(float4(
                ax + T * (bx - ax),
                ay + T * (by - ay),
                az + T * (bz - az),
                aw + T * (bw - aw)
            ));
        }
        const float Theta = std::acos(Cos);
        const float SinT = std::sin(Theta);
        const float WA = std::sin((1.0f - T) * Theta) / SinT;
        const float WB = std::sin(T * Theta) / SinT;
        return float4(WA * ax + WB * bx, WA * ay + WB * by, WA * az + WB * bz, WA * aw + WB * bw);
    }

    float4 QuatNlerp(float4 A, float4 B, float T)
    {
        const float Sign = QuatDot(A, B) < 0.0f ? -1.0f : 1.0f;
        const float ax = float(A.x), ay = float(A.y), az = float(A.z), aw = float(A.w);
        const float bx = Sign * float(B.x), by = Sign * float(B.y);
        const float bz = Sign * float(B.z), bw = Sign * float(B.w);
        return QuatNormalize(float4(
            ax + T * (bx - ax),
            ay + T * (by - ay),
            az + T * (bz - az),
            aw + T * (bw - aw)
        ));
    }

    float4x4 QuatToMatrix(float4 Q)
    {
        const float x = float(Q.x), y = float(Q.y), z = float(Q.z), w = float(Q.w);
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;
        return float4x4(
            1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz), 2.0f * (xz + wy), 0.0f,
            2.0f * (xy + wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx), 0.0f,
            2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (xx + yy), 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
    }

    float4 QuatFromMatrix(const float4x4& M)
    {
        // Shepperd's method: pick the largest of {trace, M00, M11, M22} so the
        // divisor is always well away from zero (stable at/near 180°).
        // Element (row I, col J) of the column-vector matrix is E[I*4 + J]
        // in hlslpp's row-major register order.
        alignas(16) float E[16];
        hlslpp::store(E, M);
        const float M00 = E[0], M01 = E[1], M02 = E[2];
        const float M10 = E[4], M11 = E[5], M12 = E[6];
        const float M20 = E[8], M21 = E[9], M22 = E[10];

        const float Trace = M00 + M11 + M22;
        float X, Y, Z, W;
        if (Trace > 0.0f)
        {
            const float S = std::sqrt(Trace + 1.0f) * 2.0f; // 4w
            W = 0.25f * S;
            X = (M21 - M12) / S;
            Y = (M02 - M20) / S;
            Z = (M10 - M01) / S;
        }
        else if (M00 > M11 && M00 > M22)
        {
            const float S = std::sqrt(1.0f + M00 - M11 - M22) * 2.0f; // 4x
            W = (M21 - M12) / S;
            X = 0.25f * S;
            Y = (M01 + M10) / S;
            Z = (M02 + M20) / S;
        }
        else if (M11 > M22)
        {
            const float S = std::sqrt(1.0f + M11 - M00 - M22) * 2.0f; // 4y
            W = (M02 - M20) / S;
            X = (M01 + M10) / S;
            Y = 0.25f * S;
            Z = (M12 + M21) / S;
        }
        else
        {
            const float S = std::sqrt(1.0f + M22 - M00 - M11) * 2.0f; // 4z
            W = (M10 - M01) / S;
            X = (M02 + M20) / S;
            Y = (M12 + M21) / S;
            Z = 0.25f * S;
        }
        return QuatNormalize(float4(X, Y, Z, W));
    }

    float4 QuatLookRotation(float3 Forward, float3 Up)
    {
        constexpr float kEpsSq = 1e-12f;

        float3 ZAxis = Forward;
        if (float(hlslpp::dot(ZAxis, ZAxis)) < kEpsSq)
        {
            return QuatIdentity();
        }
        ZAxis = hlslpp::normalize(ZAxis);

        float3 XAxis = hlslpp::cross(Up, ZAxis);
        if (float(hlslpp::dot(XAxis, XAxis)) < kEpsSq)
        {
            // Up parallel to forward — same fallback ladder as math::LookAtLH.
            const float3 FallbackUp = std::abs(float(ZAxis.y)) > 0.999f
                                          ? float3(0.0f, 0.0f, 1.0f)
                                          : float3(0.0f, 1.0f, 0.0f);
            XAxis = hlslpp::cross(FallbackUp, ZAxis);
        }
        XAxis = hlslpp::normalize(XAxis);
        const float3 YAxis = hlslpp::cross(ZAxis, XAxis);

        // Basis vectors are the world-space images of the local axes, i.e.
        // the COLUMNS of the rotation matrix (column-vector convention).
        return QuatFromMatrix(float4x4(
            XAxis.x, YAxis.x, ZAxis.x, 0.0f,
            XAxis.y, YAxis.y, ZAxis.y, 0.0f,
            XAxis.z, YAxis.z, ZAxis.z, 0.0f,
            0.0f,    0.0f,    0.0f,    1.0f
        ));
    }

    // =============================================================================
    // Transform
    // =============================================================================
    float4x4 Transform::ToMatrix() const
    {
        // T * R * S — column-vector convention, applied right-to-left:
        // Scale, then Rotate, then Translate.
        const float4x4 S = math::Scale(Scale);
        const float4x4 R = QuatToMatrix(Rotation);
        const float4x4 T = math::Translation(Position);
        return hlslpp::mul(T, hlslpp::mul(R, S));
    }

    float4x4 Transform::ToViewMatrix() const
    {
        // Exact rigid inverse [R^T | -R^T·P]: world-space basis vectors in
        // ROWS, translation = -(basis · position). Same shape LookAtLH
        // produces, but derived from the quaternion — no re-derivation of the
        // basis from a forward vector, so camera roll survives intact.
        const float3 XAxis = GetRight();
        const float3 YAxis = GetUp();
        const float3 ZAxis = GetForward();
        return float4x4(
            XAxis.x, XAxis.y, XAxis.z, -hlslpp::dot(XAxis, Position),
            YAxis.x, YAxis.y, YAxis.z, -hlslpp::dot(YAxis, Position),
            ZAxis.x, ZAxis.y, ZAxis.z, -hlslpp::dot(ZAxis, Position),
            0.0f,    0.0f,    0.0f,     1.0f
        );
    }

    Transform Transform::Inverse() const
    {
        // f(p) = P + R(S·p)  ⇒  f⁻¹(p) = S⁻¹·R⁻¹(p − P)
        // Re-expressed in TRS form (exact for uniform scale):
        //   rotation = R⁻¹, scale = S⁻¹, position = S⁻¹·R⁻¹(−P)
        Transform Out;
        Out.Rotation = QuatConjugate(Rotation);
        Out.Scale = float3(1.0f, 1.0f, 1.0f) / Scale;
        Out.Position = Out.Scale * QuatRotateVector(Out.Rotation, -Position);
        return Out;
    }

    Transform Transform::operator*(const Transform& Child) const
    {
        // parent * child:
        //   position = parent.position + parent.rotation * (parent.scale * child.position)
        //   rotation = parent.rotation * child.rotation
        //   scale    = parent.scale * child.scale   (component-wise; see the
        //              non-uniform-scale caveat in the file header)
        Transform Out;
        Out.Position = Position + QuatRotateVector(Rotation, Scale * Child.Position);
        Out.Rotation = QuatNormalize(QuatMul(Rotation, Child.Rotation));
        Out.Scale = Scale * Child.Scale;
        return Out;
    }

    float3 Transform::TransformPoint(float3 P) const
    {
        return Position + QuatRotateVector(Rotation, Scale * P);
    }

    float3 Transform::InverseTransformPoint(float3 P) const
    {
        return QuatUnrotateVector(Rotation, P - Position) / Scale;
    }

    float3 Transform::TransformVector(float3 V) const
    {
        return QuatRotateVector(Rotation, Scale * V);
    }

    float3 Transform::InverseTransformVector(float3 V) const
    {
        return QuatUnrotateVector(Rotation, V) / Scale;
    }

    void Transform::Translate(float3 Delta) noexcept
    {
        Position += Delta;
    }

    void Transform::Rotate(float4 Q)
    {
        Rotation = QuatNormalize(QuatMul(Q, Rotation));
    }

    void Transform::RotateLocal(float4 Q)
    {
        Rotation = QuatNormalize(QuatMul(Rotation, Q));
    }

    void Transform::RotateAxis(float3 Axis, float Radians)
    {
        Rotate(QuatFromAxisAngle(Axis, Radians));
    }

    void Transform::RotateEuler(float Pitch, float Yaw, float Roll)
    {
        Rotate(QuatFromEuler(Pitch, Yaw, Roll));
    }

    float3 Transform::GetForward() const noexcept { return QuatRotateVector(Rotation, float3(0.0f, 0.0f, 1.0f)); }
    float3 Transform::GetRight() const noexcept { return QuatRotateVector(Rotation, float3(1.0f, 0.0f, 0.0f)); }
    float3 Transform::GetUp() const noexcept { return QuatRotateVector(Rotation, float3(0.0f, 1.0f, 0.0f)); }

    float3 Transform::RotateVector(float3 LocalDir) const noexcept { return QuatRotateVector(Rotation, LocalDir); }
    float3 Transform::UnrotateVector(float3 WorldDir) const noexcept { return QuatUnrotateVector(Rotation, WorldDir); }

    void Transform::ScaleBy(float3 S) noexcept
    {
        Scale *= S;
    }
} // namespace helio
