// MeshInstanced.slang — canonical instanced static-mesh draw.
//
// One pipeline draws N instances of one mesh in one call. Bindless vertex pull
// from a storage buffer (no traditional VkVertexInputAttribute setup). The
// fragment shader does a simple diffuse N·L term so you can SEE the geometry
// — this is the verification pipeline, not the shading pipeline. Real shading
// comes later via your own pipeline using `import Vertex`.
//
// Push constants (≤128 B):
//   float4x4 ViewProj           // 64 B
//   uint     VertexBufferSlot   //  4 B
//   uint     InstanceBufferSlot //  4 B
//   uint     InstanceBase       //  4 B  start index of this draw's instances
//   uint     _padA              //  4 B  HLSL float3 alignment pad
//   float3   LightDirWS         // 12 B  (must be normalized; sun direction)
//   uint     _pad0
//   float4   AlbedoTint         // 16 B  (default 1,1,1,1)
//
// `InstanceBase` exists because Slang's `SV_InstanceID` is a per-draw counter
// (starts at 0 every draw) — it does NOT include vkCmdDrawIndexed's
// firstInstance. Multi-mesh batches pack everything into one buffer and use
// this field to tell each draw where its slice starts.

import Vertex;

struct PC {
  float4x4 ViewProj; // 64
  
  float4 LightDirWS; /// light dir + intensity // 96 - 112
  
  float3 Albedo;
  float Roughness;
  
  float3 CameraPosWS;
  float Metallic;
  
  uint VertexBufferSlot; // 112 - 116
  uint InstanceBufferSlot; // 116 - 120
  uint InstanceBase; // 120 - 124
  
};
[[vk::push_constant]]
PC pc;

struct VSOut {
  float4 Position : SV_Position;
  float3 NormalWS : NORMAL0;
  float3 WorldPos : POSITION0;
  float2 UV : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID: SV_VertexID, uint InstanceID: SV_InstanceID) {
  Vertex V = LoadVertex(pc.VertexBufferSlot, VertexID);
  float4x4 Xform = LoadInstanceTransform(pc.InstanceBufferSlot,
                                         pc.InstanceBase + InstanceID);

  float4 WorldPos = mul(Xform, float4(V.Pos, 1.0f));
  float4 ClipPos = mul(pc.ViewProj, WorldPos);

  // Normal transformed by instance rotation. Static meshes assume uniform
  // scale; for non-uniform scale you'd want the inverse-transpose, which
  // we defer to a later pass when materials need it.
  float3 NormalWS = normalize(mul((float3x3)Xform, V.Normal));

  VSOut Out;
  Out.Position = ClipPos;
  Out.NormalWS = NormalWS;
  Out.WorldPos = WorldPos.xyz;
  Out.UV = V.UV;
  return Out;
}

float DistGGX(float3 N, float3 H, float Roughness) {
  float a = Roughness * Roughness;
  float a2 = a * a;
  float NdotH = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float nom = a2;
  float denom = (NdotH * (a2 - 1.0) + 1.0);
  denom = 3.14 * denom * denom;

  return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float Roughness) {
  float r = (Roughness + 1.0);
  float k = (r * r) / 8.0;

  float nom = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float Roughness) {
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx2 = GeometrySchlickGGX(NdotV, Roughness);
  float ggx1 = GeometrySchlickGGX(NdotL, Roughness);

  return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
  float3 N = normalize(In.NormalWS);
  float3 V = normalize(pc.CameraPosWS - In.WorldPos.xyz);
  float3 L = normalize(-pc.LightDirWS.rgb);

  float3 albedo = pc.Albedo;
  float roughness = pc.Roughness;
  float metallic = pc.Metallic;

  // Linearly interpolate based on how metallic the surface is
  float3 F0_dielectric = float3(0.04);
  float3 F0_metallic = albedo;

  float3 F0 = lerp(F0_dielectric, F0_metallic, metallic);

  float3 H = normalize(V + L);

  float NDF = DistGGX(N, H, roughness);
  float G = GeometrySmith(N, V, L, roughness);
  float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

  float3 kS = F;
  float3 kD = float3(1.0) - kS;
  kD *= (1.0 - metallic);

  float3 nom = NDF * G * F;
  float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;

  // Cheap N·L + ambient so geometry is visible.
  float NdotL = max(dot(N, L), 0.0);

  float3 diffuse = (kD * albedo / 3.14);
  float3 specular = nom / denom;

  float3 Color = (diffuse + specular) * (NdotL * pc.LightDirWS.w);

  return float4(Color, 1.0f);
}
