/// @file MathTests.cpp
/// @brief Locks the engine's math conventions with exact-value checks.
///
/// Every convention that other systems (renderer, scene graph, future
/// physics) rely on is pinned here: handedness, Euler order, quaternion
/// composition, projection depth mapping, Y-flip, transform composition.
/// If a refactor changes behavior, a test names the convention it broke.

#include <Math/Math.h>
#include <Math/Transform.h>

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace helio;

namespace {

int GFailures = 0;
int GChecks = 0;

void Report(const char* Expr, const char* File, int Line, const char* Detail) {
    ++GFailures;
    std::printf("FAIL %s:%d  %s%s%s\n", File, Line, Expr, Detail[0] ? "  " : "", Detail);
}

#define CHECK(Cond)                                                    \
    do {                                                               \
        ++GChecks;                                                     \
        if (!(Cond)) Report(#Cond, __FILE__, __LINE__, "");            \
    } while (0)

#define CHECK_NEAR(A, B, Eps)                                          \
    do {                                                               \
        ++GChecks;                                                     \
        const float _a = (A), _b = (B);                                \
        if (std::abs(_a - _b) > (Eps)) {                               \
            char _buf[128];                                            \
            std::snprintf(_buf, sizeof(_buf), "(%g vs %g)", _a, _b);   \
            Report(#A " ~= " #B, __FILE__, __LINE__, _buf);            \
        }                                                              \
    } while (0)

constexpr float kEps = 1e-5f;
constexpr float kPi = math::Pi;

void CheckVec3Near(float3 V, float X, float Y, float Z, float Eps, const char* File, int Line) {
    ++GChecks;
    if (std::abs(float(V.x) - X) > Eps || std::abs(float(V.y) - Y) > Eps ||
        std::abs(float(V.z) - Z) > Eps) {
        char Buf[160];
        std::snprintf(Buf, sizeof(Buf), "got (%g, %g, %g), want (%g, %g, %g)",
                      float(V.x), float(V.y), float(V.z), X, Y, Z);
        Report("vec3", File, Line, Buf);
    }
}
#define CHECK_VEC3(V, X, Y, Z) CheckVec3Near((V), (X), (Y), (Z), kEps, __FILE__, __LINE__)

void CheckMatNear(const float4x4& A, const float4x4& B, float Eps, const char* File, int Line) {
    ++GChecks;
    alignas(16) float EA[16], EB[16];
    hlslpp::store(EA, A);
    hlslpp::store(EB, B);
    for (int I = 0; I < 16; ++I) {
        if (std::abs(EA[I] - EB[I]) > Eps) {
            char Buf[128];
            std::snprintf(Buf, sizeof(Buf), "element %d: %g vs %g", I, EA[I], EB[I]);
            Report("mat4 ~=", File, Line, Buf);
            return;
        }
    }
}
#define CHECK_MAT(A, B) CheckMatNear((A), (B), 1e-4f, __FILE__, __LINE__)

/// Same rotation iff |dot| ≈ 1 (q and -q represent the same rotation).
void CheckSameRotation(float4 A, float4 B, const char* File, int Line) {
    ++GChecks;
    const float D = std::abs(QuatDot(A, B));
    if (std::abs(D - 1.0f) > 1e-4f) {
        char Buf[192];
        std::snprintf(Buf, sizeof(Buf),
                      "|dot|=%g  A=(%g,%g,%g,%g) B=(%g,%g,%g,%g)", D,
                      float(A.x), float(A.y), float(A.z), float(A.w),
                      float(B.x), float(B.y), float(B.z), float(B.w));
        Report("same rotation", File, Line, Buf);
    }
}
#define CHECK_QUAT_ROT(A, B) CheckSameRotation((A), (B), __FILE__, __LINE__)

float Len3(float3 V) {
    return std::sqrt(float(hlslpp::dot(V, V)));
}

float3 NDC(const float4x4& Proj, float3 ViewPos) {
    const float4 Clip = hlslpp::mul(Proj, float4(ViewPos.x, ViewPos.y, ViewPos.z, 1.0f));
    const float InvW = 1.0f / float(Clip.w);
    return float3(float(Clip.x) * InvW, float(Clip.y) * InvW, float(Clip.z) * InvW);
}

// ---------------------------------------------------------------------------

void TestQuatRotatePreservesLength() {
    // Regression: QuatRotateVector must be length-preserving. It used to
    // normalize its result, corrupting non-unit vectors and NaN-ing zero.
    const float4 Q = QuatFromAxisAngle(float3(0.3f, 1.0f, -0.2f), 1.1f);
    CHECK_NEAR(Len3(QuatRotateVector(Q, float3(2.0f, 0.0f, 0.0f))), 2.0f, kEps);
    CHECK_NEAR(Len3(QuatRotateVector(Q, float3(0.3f, -4.0f, 5.0f))), Len3(float3(0.3f, -4.0f, 5.0f)), 1e-4f);
    CHECK_VEC3(QuatRotateVector(Q, float3(0.0f, 0.0f, 0.0f)), 0.0f, 0.0f, 0.0f); // no NaN
}

void TestQuatRotateMatchesMatrix() {
    const float4 Quats[] = {
        QuatIdentity(),
        QuatFromAxisAngle(float3(0, 1, 0), 0.5f * kPi),
        QuatFromAxisAngle(float3(1, 0, 0), -0.3f),
        QuatFromAxisAngle(float3(1, 1, 1), 2.4f),
        QuatFromAxisAngle(float3(-0.2f, 0.7f, 0.4f), kPi), // 180°
    };
    const float3 Vecs[] = {
        float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1), float3(-2.0f, 3.0f, 0.5f),
    };
    for (const float4& Q : Quats) {
        const float4x4 M = QuatToMatrix(Q);
        for (const float3& V : Vecs) {
            const float3 A = QuatRotateVector(Q, V);
            const float4 B = hlslpp::mul(M, float4(V.x, V.y, V.z, 0.0f));
            CHECK_VEC3(A, float(B.x), float(B.y), float(B.z));
        }
    }
}

void TestHandedness() {
    // LH, +Y up, +Z forward. Yaw +90° about +Y turns forward (+Z) to +X.
    const float4 Yaw90 = QuatFromAxisAngle(float3(0, 1, 0), 0.5f * kPi);
    CHECK_VEC3(QuatRotateVector(Yaw90, float3(0, 0, 1)), 1.0f, 0.0f, 0.0f);
    // Pitch +90° about +X takes forward (+Z) to -Y (positive pitch = look down
    // under column-vector Rx — the camera negates mouse-up accordingly).
    const float4 Pitch90 = QuatFromAxisAngle(float3(1, 0, 0), 0.5f * kPi);
    CHECK_VEC3(QuatRotateVector(Pitch90, float3(0, 0, 1)), 0.0f, -1.0f, 0.0f);
    // Roll +90° about +Z takes up (+Y) to -X? Column-vector Rz: (0,1,0) -> (-sin, cos, 0).
    const float4 Roll90 = QuatFromAxisAngle(float3(0, 0, 1), 0.5f * kPi);
    CHECK_VEC3(QuatRotateVector(Roll90, float3(0, 1, 0)), -1.0f, 0.0f, 0.0f);
}

void TestEulerComposition() {
    // QuatFromEuler must equal the explicit intrinsic chain qYaw * qPitch * qRoll.
    const float Angles[][3] = {
        {0.4f, 1.2f, -0.7f}, {-1.1f, 0.3f, 2.0f}, {0.0f, 2.7f, 0.0f},
        {1.4f, -2.2f, 0.9f}, {-0.2f, 0.0f, -3.0f},
    };
    for (const auto& A : Angles) {
        const float Pitch = A[0], Yaw = A[1], Roll = A[2];
        const float4 Chain = QuatMul(
            QuatMul(QuatFromAxisAngle(float3(0, 1, 0), Yaw),
                    QuatFromAxisAngle(float3(1, 0, 0), Pitch)),
            QuatFromAxisAngle(float3(0, 0, 1), Roll));
        CHECK_QUAT_ROT(QuatFromEuler(Pitch, Yaw, Roll), Chain);
    }
}

void TestEulerRoundTrip() {
    const float Pitches[] = {-1.4f, -0.6f, 0.0f, 0.8f, 1.5f};
    const float Yaws[] = {-3.0f, -0.9f, 0.0f, 1.7f, 2.9f};
    const float Rolls[] = {-2.5f, 0.0f, 0.4f, 3.0f};
    for (float P : Pitches)
        for (float Y : Yaws)
            for (float R : Rolls) {
                const float4 Q = QuatFromEuler(P, Y, R);
                const float3 E = QuatToEuler(Q);
                const float4 Q2 = QuatFromEuler(float(E.x), float(E.y), float(E.z));
                CHECK_QUAT_ROT(Q, Q2);
            }
    // Gimbal-lock poles.
    for (float P : {0.5f * kPi, -0.5f * kPi}) {
        const float4 Q = QuatFromEuler(P, 0.7f, 0.3f);
        const float3 E = QuatToEuler(Q);
        const float4 Q2 = QuatFromEuler(float(E.x), float(E.y), float(E.z));
        CHECK_QUAT_ROT(Q, Q2);
        CHECK_NEAR(float(E.z), 0.0f, 1e-3f); // roll forced to 0 at the pole
    }
}

void TestQuatMatrixRoundTrip() {
    const float4 Quats[] = {
        QuatIdentity(),
        QuatFromAxisAngle(float3(1, 0, 0), kPi),          // 180° — M00 branch
        QuatFromAxisAngle(float3(0, 1, 0), kPi),          // 180° — M11 branch
        QuatFromAxisAngle(float3(0, 0, 1), kPi),          // 180° — M22 branch
        QuatFromAxisAngle(float3(1, 1, 0), kPi),
        QuatFromAxisAngle(float3(0.2f, -0.5f, 0.8f), 2.9f),
        QuatFromEuler(0.7f, -1.9f, 0.4f),
    };
    for (const float4& Q : Quats) {
        CHECK_QUAT_ROT(QuatFromMatrix(QuatToMatrix(Q)), Q);
    }
}

void TestLookRotation() {
    // Aims +Z at Forward.
    const float3 Targets[] = {
        float3(1, 0, 0), float3(0, 0, -1), float3(0.5f, 0.3f, 0.8f), float3(-2, 1, -1),
    };
    for (const float3& F : Targets) {
        const float4 Q = QuatLookRotation(F);
        const float3 NF = hlslpp::normalize(F);
        const float3 Fwd = QuatRotateVector(Q, float3(0, 0, 1));
        CHECK_VEC3(Fwd, float(NF.x), float(NF.y), float(NF.z));
        // Right axis stays horizontal when up = +Y (no roll).
        CHECK_NEAR(float(QuatRotateVector(Q, float3(1, 0, 0)).y), 0.0f, kEps);
        // Up axis must point to the SAME hemisphere as world +Y — pins the
        // sign of the basis so a 180-degree roll (X'=-X, Y'=-Y, still a proper
        // rotation) can't slip through as "valid".
        CHECK(float(QuatRotateVector(Q, float3(0, 1, 0)).y) > 0.0f);
        // Exact basis for a known target: looking along +X, right = -Z, up = +Y.
    }
    {
        const float4 Q = QuatLookRotation(float3(1, 0, 0));
        CHECK_VEC3(QuatRotateVector(Q, float3(0, 0, 1)), 1.0f, 0.0f, 0.0f); // fwd = +X
        CHECK_VEC3(QuatRotateVector(Q, float3(0, 1, 0)), 0.0f, 1.0f, 0.0f); // up  = +Y
        CHECK_VEC3(QuatRotateVector(Q, float3(1, 0, 0)), 0.0f, 0.0f, -1.0f); // right = -Z (LH)
    }
    // Degenerate: forward ≈ up must not NaN, must still aim +Z at forward.
    const float4 QUp = QuatLookRotation(float3(0, 1, 0));
    CHECK_VEC3(QuatRotateVector(QUp, float3(0, 0, 1)), 0.0f, 1.0f, 0.0f);
    // Zero forward → identity.
    CHECK_QUAT_ROT(QuatLookRotation(float3(0, 0, 0)), QuatIdentity());
}

void TestSlerpNlerp() {
    const float4 A = QuatFromAxisAngle(float3(0, 1, 0), 0.0f);
    const float4 B = QuatFromAxisAngle(float3(0, 1, 0), 0.5f * kPi);
    CHECK_QUAT_ROT(QuatSlerp(A, B, 0.0f), A);
    CHECK_QUAT_ROT(QuatSlerp(A, B, 1.0f), B);
    // Midpoint of a 90° yaw is a 45° yaw.
    CHECK_QUAT_ROT(QuatSlerp(A, B, 0.5f), QuatFromAxisAngle(float3(0, 1, 0), 0.25f * kPi));
    CHECK_QUAT_ROT(QuatNlerp(A, B, 0.5f), QuatFromAxisAngle(float3(0, 1, 0), 0.25f * kPi));
    // Shortest arc: interpolating toward -B must behave like toward B.
    const float4 NegB = float4(-float(B.x), -float(B.y), -float(B.z), -float(B.w));
    CHECK_QUAT_ROT(QuatSlerp(A, NegB, 0.5f), QuatFromAxisAngle(float3(0, 1, 0), 0.25f * kPi));
    CHECK_QUAT_ROT(QuatNlerp(A, NegB, 0.5f), QuatFromAxisAngle(float3(0, 1, 0), 0.25f * kPi));

    // Constant angular velocity: on a WIDE arc, slerp at an ASYMMETRIC T must
    // land at exactly T of the angle. Nlerp deviates several degrees here, so
    // this is what distinguishes real slerp from a normalized lerp — the
    // midpoint-only checks above cannot.
    const float Wide = 2.7f; // radians (~155 deg)
    const float4 C0 = QuatFromAxisAngle(float3(0, 1, 0), 0.0f);
    const float4 C1 = QuatFromAxisAngle(float3(0, 1, 0), Wide);
    CHECK_QUAT_ROT(QuatSlerp(C0, C1, 0.25f), QuatFromAxisAngle(float3(0, 1, 0), 0.25f * Wide));
    CHECK_QUAT_ROT(QuatSlerp(C0, C1, 0.75f), QuatFromAxisAngle(float3(0, 1, 0), 0.75f * Wide));
}

void TestTransformCompose() {
    Transform A(float3(1, 2, 3), QuatFromEuler(0.3f, 1.1f, -0.4f), float3(2, 2, 2));
    Transform B(float3(-4, 0.5f, 7), QuatFromAxisAngle(float3(1, 1, 0), 0.8f), float3(0.5f, 0.5f, 0.5f));
    CHECK_MAT((A * B).ToMatrix(), hlslpp::mul(A.ToMatrix(), B.ToMatrix()));

    // Point maps agree with the matrix.
    const float3 P(0.7f, -1.2f, 2.5f);
    const float3 ViaTransform = A.TransformPoint(P);
    const float4 ViaMatrix = hlslpp::mul(A.ToMatrix(), float4(P.x, P.y, P.z, 1.0f));
    CHECK_VEC3(ViaTransform, float(ViaMatrix.x), float(ViaMatrix.y), float(ViaMatrix.z));
}

void TestTransformInverse() {
    Transform T(float3(3, -1, 5), QuatFromEuler(0.5f, -0.9f, 1.3f), float3(2.5f, 2.5f, 2.5f));
    const Transform Id = T * T.Inverse();
    CHECK_VEC3(Id.Position, 0.0f, 0.0f, 0.0f);
    CHECK_QUAT_ROT(Id.Rotation, QuatIdentity());
    CHECK_VEC3(Id.Scale, 1.0f, 1.0f, 1.0f);

    // Point round trip is exact even with non-uniform scale.
    Transform NU(float3(1, 2, 3), QuatFromEuler(0.2f, 0.6f, -1.0f), float3(2, 3, 0.5f));
    const float3 P(4.0f, -2.0f, 1.5f);
    CHECK_VEC3(NU.InverseTransformPoint(NU.TransformPoint(P)), float(P.x), float(P.y), float(P.z));
}

void TestViewMatrix() {
    Transform Cam(float3(5, 2, -8), QuatFromEuler(0.3f, 2.1f, 0.0f));
    const float4x4 View = Cam.ToViewMatrix();

    // Camera position maps to the view-space origin.
    const float4 Origin = hlslpp::mul(View, float4(Cam.Position.x, Cam.Position.y, Cam.Position.z, 1.0f));
    CHECK_VEC3(float3(Origin.xyz), 0.0f, 0.0f, 0.0f);

    // A point one unit ahead maps to (0, 0, 1).
    const float3 Ahead = Cam.Position + Cam.GetForward();
    const float4 AheadV = hlslpp::mul(View, float4(Ahead.x, Ahead.y, Ahead.z, 1.0f));
    CHECK_VEC3(float3(AheadV.xyz), 0.0f, 0.0f, 1.0f);

    // Equivalent to LookAtLH when roll-free.
    CHECK_MAT(View, math::LookAtLH(Cam.Position, Cam.Position + Cam.GetForward(), float3(0, 1, 0)));

    // Roll survives (LookAt would discard it): up axis maps to view-space +Y.
    Transform Rolled(float3(0, 0, 0), QuatFromEuler(0.4f, 1.0f, 0.9f));
    const float3 Up = Rolled.GetUp();
    const float4 UpV = hlslpp::mul(Rolled.ToViewMatrix(), float4(Up.x, Up.y, Up.z, 0.0f));
    CHECK_VEC3(float3(UpV.xyz), 0.0f, 1.0f, 0.0f);
}

void TestLookAtDegenerateUp() {
    // Looking straight down with Up = +Y used to produce NaNs. Must now fall
    // back to a valid orthonormal basis.
    const float4x4 V = math::LookAtLH(float3(0, 10, 0), float3(0, 0, 0), float3(0, 1, 0));
    alignas(16) float E[16];
    hlslpp::store(E, V);
    for (int I = 0; I < 16; ++I) CHECK(std::isfinite(E[I]));
    // Forward row (row 2) must point from eye to target: (0, -1, 0).
    CHECK_NEAR(E[8], 0.0f, kEps);
    CHECK_NEAR(E[9], -1.0f, kEps);
    CHECK_NEAR(E[10], 0.0f, kEps);
}

void TestPerspectiveReverseZ() {
    const float Near = 0.1f;
    const float4x4 P = math::PerspectiveReverseZLH(0.5f * kPi, 2.0f, Near);
    // Depth: near plane -> 1, receding -> 0.
    CHECK_NEAR(float(NDC(P, float3(0, 0, Near)).z), 1.0f, kEps);
    CHECK_NEAR(float(NDC(P, float3(0, 0, 10.0f * Near)).z), 0.1f, kEps);
    CHECK_NEAR(float(NDC(P, float3(0, 0, 10000.0f)).z), 0.0f, 1e-4f);
    // Y-flip: a point above the view axis lands in negative (Vulkan-up) clip y.
    CHECK(float(NDC(P, float3(0, 1, 2)).y) < 0.0f);
    // FovY 90° → at z, y = z hits the top edge: |ndc.y| = 1.
    CHECK_NEAR(float(NDC(P, float3(0, 2, 2)).y), -1.0f, kEps);
    // Aspect 2 → x = 2z hits the right edge.
    CHECK_NEAR(float(NDC(P, float3(4, 0, 2)).x), 1.0f, kEps);
}

void TestOrthoReverseZ() {
    const float W = 20.0f, H = 10.0f, N = 1.0f, F = 51.0f;
    const float4x4 P = math::OrthoReverseZLH(W, H, N, F);
    // Depth: near -> 1, far -> 0, midpoint -> 0.5 (linear).
    CHECK_NEAR(float(NDC(P, float3(0, 0, N)).z), 1.0f, kEps);
    CHECK_NEAR(float(NDC(P, float3(0, 0, F)).z), 0.0f, kEps);
    CHECK_NEAR(float(NDC(P, float3(0, 0, 0.5f * (N + F))).z), 0.5f, kEps);
    // X: ±W/2 -> ±1.
    CHECK_NEAR(float(NDC(P, float3(+0.5f * W, 0, N)).x), +1.0f, kEps);
    CHECK_NEAR(float(NDC(P, float3(-0.5f * W, 0, N)).x), -1.0f, kEps);
    // Y: +H/2 (world up) -> -1 (Vulkan up). Same flip as the perspective.
    CHECK_NEAR(float(NDC(P, float3(0, +0.5f * H, N)).y), -1.0f, kEps);
    CHECK_NEAR(float(NDC(P, float3(0, -0.5f * H, N)).y), +1.0f, kEps);

    // Off-center variant maps its bounds the same way.
    const float4x4 OC = math::OrthoOffCenterReverseZLH(2.0f, 6.0f, -3.0f, 1.0f, N, F);
    CHECK_NEAR(float(NDC(OC, float3(2, 0, N)).x), -1.0f, kEps);
    CHECK_NEAR(float(NDC(OC, float3(6, 0, N)).x), +1.0f, kEps);
    CHECK_NEAR(float(NDC(OC, float3(0, -3, N)).y), +1.0f, kEps);
    CHECK_NEAR(float(NDC(OC, float3(0, 1, N)).y), -1.0f, kEps);
    // Centered == off-center with symmetric bounds.
    CHECK_MAT(P, math::OrthoOffCenterReverseZLH(-0.5f * W, 0.5f * W, -0.5f * H, 0.5f * H, N, F));
}

void TestAABB() {
    math::AABB Box; // default = empty
    CHECK(!Box.IsValid());
    Box.Expand(float3(1, 2, 3));
    CHECK(Box.IsValid());
    Box.Expand(float3(-1, 0, 5));
    CHECK_VEC3(Box.Min, -1.0f, 0.0f, 3.0f);
    CHECK_VEC3(Box.Max, 1.0f, 2.0f, 5.0f);
    CHECK(Box.Contains(float3(0, 1, 4)));
    CHECK(!Box.Contains(float3(0, 3, 4)));

    // Unit cube rotated 45° about Y: x/z extents grow to √2, y unchanged.
    math::AABB Unit;
    Unit.Expand(float3(-1, -1, -1));
    Unit.Expand(float3(1, 1, 1));
    const math::AABB Rot = Unit.Transformed(math::RotationY(0.25f * kPi));
    const float Sqrt2 = std::sqrt(2.0f);
    CHECK_VEC3(Rot.Max, Sqrt2, 1.0f, Sqrt2);
    CHECK_VEC3(Rot.Min, -Sqrt2, -1.0f, -Sqrt2);

    // Translation moves the box wholesale.
    const math::AABB Moved = Unit.Transformed(math::Translation(10, 0, -5));
    CHECK_VEC3(Moved.Center(), 10.0f, 0.0f, -5.0f);

    // Empty boxes stay empty through transforms.
    CHECK(!math::AABB{}.Transformed(math::RotationX(1.0f)).IsValid());

    // Corner indexing: bit 0 = x, bit 1 = y, bit 2 = z.
    CHECK_VEC3(Box.Corner(0), -1.0f, 0.0f, 3.0f);
    CHECK_VEC3(Box.Corner(7), 1.0f, 2.0f, 5.0f);
    CHECK_VEC3(Box.Corner(5), 1.0f, 0.0f, 5.0f);
}

void TestTransformMutators() {
    Transform T;
    T.Translate(1, 2, 3);
    T.Translate(float3(1, 0, -1));
    CHECK_VEC3(T.Position, 2.0f, 2.0f, 2.0f);

    // World-space Rotate vs local-space RotateLocal: starting from a 90° yaw,
    // a world-space pitch rotates about world X; a local pitch about the
    // (now sideways) local X.
    Transform WorldRot;
    WorldRot.RotateAxis(float3(0, 1, 0), 0.5f * kPi);
    WorldRot.Rotate(QuatFromAxisAngle(float3(1, 0, 0), 0.5f * kPi));
    // forward: +Z --yaw--> +X --world pitch about X--> +X (unchanged).
    CHECK_VEC3(WorldRot.GetForward(), 1.0f, 0.0f, 0.0f);

    Transform LocalRot;
    LocalRot.RotateAxis(float3(0, 1, 0), 0.5f * kPi);
    LocalRot.RotateLocal(QuatFromAxisAngle(float3(1, 0, 0), 0.5f * kPi));
    // local pitch tilts the local forward down to -Y regardless of yaw.
    CHECK_VEC3(LocalRot.GetForward(), 0.0f, -1.0f, 0.0f);

    T = Transform();
    T.ScaleBy(2.0f);
    T.ScaleBy(float3(1, 2, 3));
    CHECK_VEC3(T.Scale, 2.0f, 4.0f, 6.0f);
}

} // namespace

int main() {
    TestQuatRotatePreservesLength();
    TestQuatRotateMatchesMatrix();
    TestHandedness();
    TestEulerComposition();
    TestEulerRoundTrip();
    TestQuatMatrixRoundTrip();
    TestLookRotation();
    TestSlerpNlerp();
    TestTransformCompose();
    TestTransformInverse();
    TestViewMatrix();
    TestLookAtDegenerateUp();
    TestPerspectiveReverseZ();
    TestOrthoReverseZ();
    TestAABB();
    TestTransformMutators();

    if (GFailures == 0) {
        std::printf("OK: %d checks passed\n", GChecks);
        return 0;
    }
    std::printf("FAILED: %d of %d checks\n", GFailures, GChecks);
    return 1;
}
