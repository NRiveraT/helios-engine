// ImGuiPass.slang — Dear ImGui draw-list rendering, Helio-bindless style.
//
// No vertex-input state and no index buffer binding: indices AND vertices are
// pulled from bindless storage buffers by SV_VertexID (the same programmable
// vertex pulling every other Helio pass uses). One non-indexed Draw per
// ImDrawCmd; push constants carry that command's buffer slots + offsets.
//
// ImDrawVert layout (20 bytes): float2 Pos (px), float2 UV, u32 Color
// (0xAABBGGRR). Indices are u16. Vertex colors and the font atlas are
// authored in sRGB; the render target is an sRGB format, so we convert
// colors to linear here and let the target's encode round-trip them.
//
// Clip transform: ImGui positions are framebuffer pixels, origin top-left,
// Y-down — exactly Vulkan's clip convention after `pos * Scale + Translate`
// with Scale = 2/DisplaySize, Translate = -1. No Y-flip, standard viewport.
//
// CPU mirror: `helio::editor::ImGuiPushConsts` (Source/Editor/ImGuiRenderer.h).

import Bindless;

struct PC {
    float2 Scale;      //  0 — 2 / DisplaySize
    float2 Translate;  //  8 — -1 - DisplayPos * Scale

    uint VertexBufferSlot; // 16
    uint IndexBufferSlot;  // 20
    uint TextureSlot;      // 24
    uint VertexOffset;     // 28 — ImDrawCmd::VtxOffset + per-list base

    uint IndexOffset;      // 32 — ImDrawCmd::IdxOffset + per-list base
    uint _Pad0, _Pad1, _Pad2;
};
[[vk::push_constant]]
PC pc;

static const uint kImDrawVertStride = 20;

struct VSOut {
    float4 Position : SV_Position;
    float4 Color : COLOR0;
    float2 UV : TEXCOORD0;
};

uint LoadIndexU16(ByteAddressBuffer B, uint Index) {
    uint ByteAddr = Index * 2;
    uint Word = B.Load(ByteAddr & ~3u);
    return (ByteAddr & 2u) != 0 ? (Word >> 16) & 0xFFFFu : Word & 0xFFFFu;
}

float3 SrgbToLinear(float3 C) {
    float3 Low = C / 12.92;
    float3 High = pow((C + 0.055) / 1.055, 2.4);
    return select(C <= 0.04045, Low, High);
}

[shader("vertex")]
VSOut VSMain(uint VertexID: SV_VertexID) {
    ByteAddressBuffer Indices = GetStorageBuffer(pc.IndexBufferSlot);
    ByteAddressBuffer Verts = GetStorageBuffer(pc.VertexBufferSlot);

    uint Index = LoadIndexU16(Indices, pc.IndexOffset + VertexID);
    uint Base = (pc.VertexOffset + Index) * kImDrawVertStride;

    float2 PosPx = asfloat(Verts.Load2(Base + 0));
    float2 UV = asfloat(Verts.Load2(Base + 8));
    uint Packed = Verts.Load(Base + 16);

    float4 Color = float4(
        float((Packed >> 0) & 0xFFu),
        float((Packed >> 8) & 0xFFu),
        float((Packed >> 16) & 0xFFu),
        float((Packed >> 24) & 0xFFu)) / 255.0;

    VSOut Out;
    Out.Position = float4(PosPx * pc.Scale + pc.Translate, 0.0, 1.0);
    Out.Color = float4(SrgbToLinear(Color.rgb), Color.a);
    Out.UV = UV;
    return Out;
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    float4 Tex = GetTexture2D(pc.TextureSlot).Sample(GetSampler(kSamplerLinearClamp), In.UV);
    return In.Color * Tex;
}
