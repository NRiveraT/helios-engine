// MeshInstanced.slang — canonical instanced static-mesh draw.
//
// One pipeline draws N instances of one mesh in one call. Bindless vertex
// pull from a storage buffer (no VkVertexInputAttribute setup). Lighting is
// a single-directional-light Cook-Torrance GGX evaluated PER FRAGMENT (never
// per vertex — specular does not survive Gouraud interpolation), shadowed by
// the sun shadow map via 3x3 hardware-PCF (see Common/Frame.slang).
//
// Per-frame data (ViewProj, light, shadow matrix + params) comes from the
// FrameConstants bindless buffer; push constants carry only per-draw slots
// and material scalars:
//
//   uint   FrameSlot            //  0 — FrameConstants bindless slot
//   uint   VertexBufferSlot     //  4
//   uint   InstanceBufferSlot   //  8
//   uint   InstanceBase         // 12 — see below
//   float3 Albedo               // 16
//   float  Roughness            // 28
//   float3 Emissive             // 32 — premultiplied EmissiveColor * Intensity
//   float  Metallic             // 44
//                               // 48 bytes total — mirror of
//                               // helio::resource::MeshInstancedPushConsts.
//
// `InstanceBase` exists because `SV_InstanceID` is a per-draw counter
// (starts at 0 every draw) — it does NOT include vkCmdDrawIndexed's
// firstInstance. Multi-mesh batches pack everything into one buffer and use
// this field to tell each draw where its slice starts.

import Vertex;
import Frame;
import Bindless;
import GPULight;

static const float kPi = 3.14159265358979323846;
// Sentinel matching resource::kNoTexture — the slot has no texture, use factor.
static const uint kNoTexture = 0xFFFFFFFF;

struct PC {
    uint FrameSlot;
    uint VertexBufferSlot;
    uint InstanceBufferSlot;
    uint InstanceBase;

    float3 Albedo;
    float Roughness;

    float3 Emissive;
    float Metallic;

    // Material texture bindless slots (kNoTexture = none). These are per-draw
    // (uniform across the draw), so the `!= kNoTexture` branches below are
    // uniform control flow — no warp divergence.
    uint AlbedoTex;
    uint NormalTex;
    uint MetalRoughTex;
    uint EmissiveTex;
    uint OcclusionTex;
    
    uint AOSamplerSlot;
};
[[vk::push_constant]]
PC pc;

struct VSOut {
    float4 Position : SV_Position;
    float3 NormalWS : NORMAL0;
    float3 WorldPos : POSITION0;
    float2 UV : TEXCOORD0;
    float4 TangentWS : TANGENT0; // xyz = world tangent, w = bitangent sign
};

struct PSOut {
	float4 Color : SV_Target0;
	float4 Normal : SV_Target1;
};

[shader("vertex")]
VSOut VSMain(uint VertexID: SV_VertexID, uint InstanceID: SV_InstanceID) {
    Vertex V = LoadVertex(pc.VertexBufferSlot, VertexID);
    float4x4 Xform = LoadInstanceTransform(pc.InstanceBufferSlot, pc.InstanceBase + InstanceID);

    float4 WorldPos = mul(Xform, float4(V.Pos, 1.0));
    FrameConstants Frame = LoadFrameConstants(pc.FrameSlot);

    // Normals transform by the inverse-transpose of the model matrix. The
    // cofactor matrix (rows = cross products of the model rows) is
    // proportional to it, so `normalize(Cof * n)` is exact for ANY affine
    // transform — non-uniform scale included — without a CPU-side inverse.
    float3x3 M = (float3x3)Xform;
    float3x3 Cof = float3x3(cross(M[1], M[2]), cross(M[2], M[0]), cross(M[0], M[1]));

    VSOut Out;
    Out.Position = mul(Frame.ViewProj, WorldPos);
    Out.NormalWS = normalize(mul(Cof, V.Normal));
    Out.WorldPos = WorldPos.xyz;
    Out.UV = V.UV;
    // Tangents transform by the model matrix itself (they live in the surface,
    // not its dual), unlike normals. Bitangent sign passes through untouched.
    Out.TangentWS = float4(normalize(mul(M, V.Tangent.xyz)), V.Tangent.w);
    return Out;
}

// ---- Cook-Torrance GGX ------------------------------------------------------

float DistGGX(float3 N, float3 H, float Roughness) {
    float A = Roughness * Roughness;
    float A2 = A * A;
    float NdotH = max(dot(N, H), 0.0);
    float Denom = (NdotH * NdotH * (A2 - 1.0) + 1.0);
    return A2 / (kPi * Denom * Denom);
}

float GeometrySchlickGGX(float NdotV, float Roughness) {
    float R = (Roughness + 1.0);
    float K = (R * R) / 8.0;
    return NdotV / (NdotV * (1.0 - K) + K);
}

float GeometrySmith(float3 N, float3 V, float3 L, float Roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, Roughness) * GeometrySchlickGGX(NdotL, Roughness);
}

float3 FresnelSchlick(float CosTheta, float3 F0) {
    // saturate the base: dot products of normalize()'d vectors can round to
    // 1.0 + a few ULP at a highlight center, making (1 - CosTheta) slightly
    // negative — and pow() of a negative base is NaN on GLSL.std.450.
    return F0 + (1.0 - F0) * pow(saturate(1.0 - CosTheta), 5.0);
}

float3 EvaluateLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float roughness, float metallic, float3 F0)
{
	float3 H = normalize(V + L);
	float NDF = DistGGX(N, H, roughness);
	float G = GeometrySmith(N, V, L, roughness);
	float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
	
	float3 KD = (float3(1.0) - F) * (1.0 - metallic);
	
	float NdotL = max(dot(N, L), 0.0);
	float3 Diffuse = KD * albedo / kPi;
	float3 Specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.001);
	
	return (Diffuse + Specular) * radiance * NdotL;
}

[shader("fragment")]
PSOut PSMain(VSOut In) : SV_Target {
    FrameConstants Frame = LoadFrameConstants(pc.FrameSlot);
    SamplerState Samp = GetSampler(kSamplerLinearWrap);
    float2 UV = In.UV;
    
    float2 ScreenUV = In.Position.xy / Frame.ViewportAO.xy;

    // ---- Material: factor × texture (uniform branch per draw) ----------------
    float3 Albedo = pc.Albedo;
    if (pc.AlbedoTex != kNoTexture) {
        // Base color texture is sRGB — the hardware linearizes on sample.
        Albedo *= GetTexture2D(pc.AlbedoTex).Sample(Samp, UV).rgb;
    }
    
    float Roughness = pc.Roughness;
    float Metallic = pc.Metallic;
    if (pc.MetalRoughTex != kNoTexture) {
        // glTF packs roughness in G, metallic in B (linear).
        float3 MR = GetTexture2D(pc.MetalRoughTex).Sample(Samp, UV).rgb;
        Roughness *= MR.g;
        Metallic *= MR.b;
    }
    // Floor roughness: at exactly 0, DistGGX's denominator is 0/0 = NaN at the
    // highlight center (NdotH == 1). 0.045 keeps a tight-but-finite highlight.
    Roughness = clamp(Roughness, 0.045, 1.0);
    Metallic = saturate(Metallic);

    float3 Emissive = pc.Emissive;
    if (pc.EmissiveTex != kNoTexture) {
        Emissive *= GetTexture2D(pc.EmissiveTex).Sample(Samp, UV).rgb;
    }

    float AO = 1.0;
    if (pc.OcclusionTex != kNoTexture) {
        AO = GetTexture2D(pc.OcclusionTex).Sample(Samp, UV).r;
    }

    // ---- Normal (geometric, or perturbed by a tangent-space normal map) ------
    float3 N = normalize(In.NormalWS);
    if (pc.NormalTex != kNoTexture) {
        // Re-orthonormalize the interpolated tangent against the normal (TBN),
        // then rotate the sampled tangent-space normal into world space.
        float3 T = normalize(In.TangentWS.xyz - N * dot(N, In.TangentWS.xyz));
        float3 B = cross(N, T) * In.TangentWS.w;
        float3 Ntex = GetTexture2D(pc.NormalTex).Sample(Samp, UV).xyz * 2.0 - 1.0;
        N = normalize(T * Ntex.x + B * Ntex.y + N * Ntex.z);
    }
 
    float3 V = normalize(Frame.CameraPosWS.xyz - In.WorldPos);
    float3 F0 = lerp(float3(0.04), Albedo, Metallic);

    float3 L = -Frame.LightDirWS.xyz; // toward the light (dir is normalized on CPU)
    float3 Radiance = Frame.LightColorAmbient.rgb * Frame.LightDirWS.w;

	float3 lit = EvaluateLight(N, V, L, Radiance, Albedo, Roughness, Metallic, F0);
    float Shadow = SampleSunShadow(Frame, In.WorldPos, N);
	lit *= Shadow;
	
	for(uint i = 0; i < Frame.LightCount; i++){
		GPULight light = LoadGPULight(Frame.LightBufferSlot, i);
		float3 toLight = light.Position - In.WorldPos;
		float dist = length(toLight);
		float3 l = toLight / dist;
		
		float attenuation = 1.0 / (dist * dist);
		float t = saturate(1.0 - pow(dist / light.Range, 4.0));
		attenuation *= t * t;
		
		lit += EvaluateLight(N, V, normalize(l), light.Color * 100.0 * attenuation, Albedo, Roughness, Metallic, F0);
	}
	
    float ao = GetTexture2D(pc.AOSamplerSlot).Sample(GetSampler(kSamplerPointClamp), ScreenUV).r;
    
    float3 Direct = lit;
    float3 Ambient = Frame.LightColorAmbient.w * Albedo * (Frame.LightColorAmbient.rgb) * (AO * ao);

	PSOut o;
	o.Color = float4(Direct + Ambient + Emissive, 1.0);
	o.Normal = float4(N, 1.0);

    return o;
}
