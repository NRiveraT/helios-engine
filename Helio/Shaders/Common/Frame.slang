// Frame.slang — per-frame constants, shared by every scene pass.
//
// CPU mirror: `helio::scene::FrameConstants` (Source/Scene/SceneRenderer.h).
// The struct lives in a bindless storage buffer whose slot arrives via push
// constants; matrices are stored through `Mat4Packed` (hlslpp
// store_transposed), so each 16-byte chunk is a COLUMN of the matrix and we
// rebuild with a transpose — identical convention to LoadInstanceTransform.
//
// Byte layout (208 bytes total — keep in lockstep with the CPU struct):
//   0   float4x4 ViewProj
//   64  float4x4 LightViewProj
//   128 float4   CameraPosWS        (xyz, w unused)
//   144 float4   LightDirWS         (xyz = travel direction, normalized; w = intensity)
//   160 float4   LightColorAmbient  (rgb = light color; w = flat ambient)
//   176 float4   ShadowParams       (x = shadow uv texel size,
//                                    y = normal-offset distance, world units,
//                                    z = receiver depth bias, reverse-Z NDC,
//                                    w = 1 when shadows are enabled this frame)
//   192 uint     ShadowMapSlot
//   196 uint3    padding

import Bindless;

struct FrameConstants {
    float4x4 ViewProj;
    float4x4 LightViewProj;
    float4 CameraPosWS;
    float4 LightDirWS;
    float4 LightColorAmbient;
    float4 ShadowParams;
    uint ShadowMapSlot;
};

float4x4 LoadMat4ColumnStored(ByteAddressBuffer B, uint Offset) {
    float4 C0 = asfloat(B.Load4(Offset + 0));  // column 0 of M
    float4 C1 = asfloat(B.Load4(Offset + 16)); // column 1
    float4 C2 = asfloat(B.Load4(Offset + 32)); // column 2
    float4 C3 = asfloat(B.Load4(Offset + 48)); // column 3
    // float4x4(R0..R3) builds rows; ours are columns — transpose back.
    return transpose(float4x4(C0, C1, C2, C3));
}

FrameConstants LoadFrameConstants(uint Slot) {
    ByteAddressBuffer B = GetStorageBuffer(Slot);
    FrameConstants F;
    F.ViewProj = LoadMat4ColumnStored(B, 0);
    F.LightViewProj = LoadMat4ColumnStored(B, 64);
    F.CameraPosWS = asfloat(B.Load4(128));
    F.LightDirWS = asfloat(B.Load4(144));
    F.LightColorAmbient = asfloat(B.Load4(160));
    F.ShadowParams = asfloat(B.Load4(176));
    F.ShadowMapSlot = B.Load(192);
    return F;
}

// 3x3 PCF over the sun shadow map with normal-offset + receiver bias.
// Returns the LIT fraction in [0, 1] (1 = fully lit). The comparison sampler
// (GREATER_OR_EQUAL, reverse-Z) does a hardware 2x2 PCF per tap, so 9 taps
// resolve a smooth ~6x6 footprint.
float SampleSunShadow(FrameConstants F, float3 WorldPos, float3 NormalWS) {
    if (F.ShadowParams.w < 0.5) {
        return 1.0; // shadows disabled this frame (no light or no casters)
    }

    // Normal-offset: sample as if the receiver sat ~1.5 texels above the
    // surface, killing self-shadow acne on slopes far more robustly than
    // depth bias alone.
    float3 Offset = NormalWS * F.ShadowParams.y;
    float4 Clip = mul(F.LightViewProj, float4(WorldPos + Offset, 1.0));
    float3 NDC = Clip.xyz / Clip.w;

    // Y-negated projection means ndc.y already points down, matching texture
    // v — the same matrix rendered the map, so orientation agrees by
    // construction.
    float2 UV = NDC.xy * 0.5 + 0.5;
    if (any(UV < 0.0) || any(UV > 1.0) || NDC.z < 0.0 || NDC.z > 1.0) {
        return 1.0; // outside the fitted light frustum: assume lit
    }

    // Reverse-Z: larger depth = closer to the light. Nudging the reference
    // TOWARD the light (up) keeps receivers from shadowing themselves.
    float Ref = NDC.z + F.ShadowParams.z;

    Texture2D<float4> ShadowMap = GetTexture2D(F.ShadowMapSlot);
    SamplerComparisonState Cmp = GetSamplerCmp(kSamplerShadowLinear);

    float Texel = F.ShadowParams.x;
    float Sum = 0.0;
    [unroll]
    for (int Y = -1; Y <= 1; ++Y) {
        [unroll]
        for (int X = -1; X <= 1; ++X) {
            Sum += ShadowMap.SampleCmpLevelZero(Cmp, UV + float2(X, Y) * Texel, Ref);
        }
    }
    return Sum * (1.0 / 9.0);
}
