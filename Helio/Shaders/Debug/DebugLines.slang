// DebugLines.slang — single line-list pipeline used by Helio's debug-draw.
//
// Box / Sphere / Frustum / Capsule wireframes are expanded into line vertices
// on the CPU and concatenated into the same buffer, so this one shader covers
// every primitive shape in the debug-draw module.
//
// Vertex layout (16 bytes, must match `helio::render::debug::LineVertex`):
//   float3 Pos
//   uint   RGBA  (0xAABBGGRR)
//
// Push constants:
//   float4x4 ViewProj
//   uint     VertexBufferSlot — bindless storage-buffer slot of the LineVertex array

import Bindless;

struct PushConsts {
    float4x4 ViewProj;
    uint     VertexBufferSlot;
    uint     _Pad0;
    uint     _Pad1;
    uint     _Pad2;
};
[[vk::push_constant]] PushConsts pc;

static const uint kVertexStrideBytes = 16;

struct VSOut {
    float4 Position : SV_Position;
    float4 Color    : COLOR0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID : SV_VertexID) {
    ByteAddressBuffer Verts = GetStorageBuffer(pc.VertexBufferSlot);
    uint   Base = VertexID * kVertexStrideBytes;
    float3 Pos  = asfloat(Verts.Load3(Base));
    uint   RGBA = Verts.Load(Base + 12);

    VSOut Out;
    Out.Position = mul(pc.ViewProj, float4(Pos, 1.0f));
    Out.Color = float4(
        ((RGBA      ) & 0xFFu) / 255.0f,
        ((RGBA >>  8) & 0xFFu) / 255.0f,
        ((RGBA >> 16) & 0xFFu) / 255.0f,
        ((RGBA >> 24) & 0xFFu) / 255.0f
    );
    return Out;
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    if (In.Color.a < 0.01f) discard;
    return float4(In.Color.rgb, 1.0f);
}
