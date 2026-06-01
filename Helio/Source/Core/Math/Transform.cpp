#include "Transform.h"

#include <cmath>

namespace helio {

// =============================================================================
// Quaternion helpers
// =============================================================================
float4 QuatFromAxisAngle(float3 Axis, float Radians) {
    const float Half = 0.5f * Radians;
    const float S    = std::sin(Half);
    const float C    = std::cos(Half);
    const float3 N   = hlslpp::normalize(Axis);
    return float4(float(N.x) * S, float(N.y) * S, float(N.z) * S, C);
}

float4 QuatFromEuler(float Pitch, float Yaw, float Roll) {
    // Intrinsic Tait-Bryan: Yaw (Y) → Pitch (X) → Roll (Z), composed as
    // q = qYaw * qPitch * qRoll. Matches Unity / Unreal conventions for the
    // common "FPS camera" mental model.
    const float HP = 0.5f * Pitch, HY = 0.5f * Yaw, HR = 0.5f * Roll;
    const float cp = std::cos(HP), sp = std::sin(HP);
    const float cy = std::cos(HY), sy = std::sin(HY);
    const float cr = std::cos(HR), sr = std::sin(HR);
    return QuatNormalize(float4(
        sp*cy*cr - cp*sy*sr,
        cp*sy*cr + sp*cy*sr,
        cp*cy*sr - sp*sy*cr,
        cp*cy*cr + sp*sy*sr
    ));
}

float4 QuatMul(float4 A, float4 B) {
    const float ax = float(A.x), ay = float(A.y), az = float(A.z), aw = float(A.w);
    const float bx = float(B.x), by = float(B.y), bz = float(B.z), bw = float(B.w);
    return float4(
        aw*bx + ax*bw + ay*bz - az*by,
        aw*by - ax*bz + ay*bw + az*bx,
        aw*bz + ax*by - ay*bx + az*bw,
        aw*bw - ax*bx - ay*by - az*bz
    );
}

float4 QuatNormalize(float4 Q) {
    const float x = float(Q.x), y = float(Q.y), z = float(Q.z), w = float(Q.w);
    const float Len2 = x*x + y*y + z*z + w*w;
    if (Len2 <= 1e-12f) return QuatIdentity();
    const float Inv = 1.0f / std::sqrt(Len2);
    return float4(x * Inv, y * Inv, z * Inv, w * Inv);
}

float4 QuatConjugate(float4 Q) {
    return float4(-float(Q.x), -float(Q.y), -float(Q.z), float(Q.w));
}

float4 QuatSlerp(float4 A, float4 B, float T) {
    float ax = float(A.x), ay = float(A.y), az = float(A.z), aw = float(A.w);
    float bx = float(B.x), by = float(B.y), bz = float(B.z), bw = float(B.w);

    float Cos = ax*bx + ay*by + az*bz + aw*bw;
    // Pick shortest arc.
    if (Cos < 0.0f) { bx = -bx; by = -by; bz = -bz; bw = -bw; Cos = -Cos; }
    // Nearly-parallel — linear interpolate to avoid divide-by-near-zero.
    if (Cos > 0.9995f) {
        return QuatNormalize(float4(
            ax + T*(bx - ax),
            ay + T*(by - ay),
            az + T*(bz - az),
            aw + T*(bw - aw)
        ));
    }
    const float Theta  = std::acos(Cos);
    const float SinT   = std::sin(Theta);
    const float WA     = std::sin((1.0f - T) * Theta) / SinT;
    const float WB     = std::sin(T * Theta) / SinT;
    return float4(WA*ax + WB*bx, WA*ay + WB*by, WA*az + WB*bz, WA*aw + WB*bw);
}

float4x4 QuatToMatrix(float4 Q) {
    const float x = float(Q.x), y = float(Q.y), z = float(Q.z), w = float(Q.w);
    const float xx = x*x, yy = y*y, zz = z*z;
    const float xy = x*y, xz = x*z, yz = y*z;
    const float wx = w*x, wy = w*y, wz = w*z;
    return float4x4(
        1.0f - 2.0f*(yy+zz),   2.0f*(xy-wz),         2.0f*(xz+wy),         0.0f,
        2.0f*(xy+wz),          1.0f - 2.0f*(xx+zz),  2.0f*(yz-wx),         0.0f,
        2.0f*(xz-wy),          2.0f*(yz+wx),         1.0f - 2.0f*(xx+yy),  0.0f,
        0.0f,                  0.0f,                 0.0f,                 1.0f
    );
}

// =============================================================================
// Transform
// =============================================================================
float4x4 Transform::ToMatrix() const {
    // T * R * S — column-vector convention, applied right-to-left:
    // Scale, then Rotate, then Translate.
    const float4x4 S = math::Scale(Scale);
    const float4x4 R = QuatToMatrix(Rotation);
    const float4x4 T = math::Translation(Position);
    return hlslpp::mul(T, hlslpp::mul(R, S));
}

Transform Transform::operator*(const Transform& Child) const {
    // parent * child:
    //   position = parent.position + parent.rotation * (parent.scale * child.position)
    //   rotation = parent.rotation * child.rotation
    //   scale    = parent.scale * child.scale (component-wise — assumes uniform-ish)
    Transform Out{};
    // Scaled-then-rotated child position, added to parent position.
    const float3 ScaledP = float3(
        float(Child.Position.x) * float(Scale.x),
        float(Child.Position.y) * float(Scale.y),
        float(Child.Position.z) * float(Scale.z)
    );
    // Rotate via q * p * q^-1 in pure-quaternion form. Cheaper as matrix multiply.
    const float4x4 RotMat = QuatToMatrix(Rotation);
    const float4 RotatedH = hlslpp::mul(RotMat, float4(ScaledP.x, ScaledP.y, ScaledP.z, 0.0f));
    Out.Position = float3(
        float(Position.x) + float(RotatedH.x),
        float(Position.y) + float(RotatedH.y),
        float(Position.z) + float(RotatedH.z)
    );
    Out.Rotation = QuatNormalize(QuatMul(Rotation, Child.Rotation));
    Out.Scale = float3(
        float(Scale.x) * float(Child.Scale.x),
        float(Scale.y) * float(Child.Scale.y),
        float(Scale.z) * float(Child.Scale.z)
    );
    return Out;
}

void Transform::Translate(float3 Delta) noexcept {
    Position = float3(
        float(Position.x) + float(Delta.x),
        float(Position.y) + float(Delta.y),
        float(Position.z) + float(Delta.z)
    );
}

void Transform::Rotate(float4 Q) {
    Rotation = QuatNormalize(QuatMul(Q, Rotation));
}

void Transform::RotateAxis(float3 Axis, float Radians) {
    Rotate(QuatFromAxisAngle(Axis, Radians));
}

void Transform::RotateEuler(float Pitch, float Yaw, float Roll) {
    Rotate(QuatFromEuler(Pitch, Yaw, Roll));
}

void Transform::ScaleBy(float3 S) noexcept {
    Scale = float3(
        float(Scale.x) * float(S.x),
        float(Scale.y) * float(S.y),
        float(Scale.z) * float(S.z)
    );
}

} // namespace helio
