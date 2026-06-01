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
//   float3   LightDirWS         // 12 B (must be normalized; sun direction)
//   uint     _pad
//   float4   AlbedoTint         // 16 B (default 1,1,1,1)

import Vertex;

struct PC {
    float4x4 ViewProj;
    uint     VertexBufferSlot;
    uint     InstanceBufferSlot;
    float3   LightDirWS;
    uint     _Pad0;
    float4   AlbedoTint;
};
[[vk::push_constant]] PC pc;

struct VSOut {
    float4 Position : SV_Position;
    float3 NormalWS : NORMAL0;
    float2 UV       : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID) {
    Vertex   V       = LoadVertex(pc.VertexBufferSlot, VertexID);
    float4x4 Xform   = LoadInstanceTransform(pc.InstanceBufferSlot, InstanceID);

    float4 WorldPos  = mul(Xform, float4(V.Pos, 1.0f));
    float4 ClipPos   = mul(pc.ViewProj, WorldPos);

    // Normal transformed by instance rotation. Static meshes assume uniform
    // scale; for non-uniform scale you'd want the inverse-transpose, which
    // we defer to a later pass when materials need it.
    float3 NormalWS  = normalize(mul((float3x3)Xform, V.Normal));

    VSOut Out;
    Out.Position = ClipPos;
    Out.NormalWS = NormalWS;
    Out.UV       = V.UV;
    return Out;
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    // Cheap N·L + ambient so geometry is visible.
    float NdotL    = saturate(dot(normalize(In.NormalWS), -pc.LightDirWS));
    float3 Color   = pc.AlbedoTint.rgb * NdotL;
    return float4(Color, 1.0f);
}
