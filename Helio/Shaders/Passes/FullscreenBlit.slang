// FullscreenBlit.slang — fullscreen triangle that samples a bindless 2D texture.
// Push constants pick the texture + sampler slots.

import Bindless;

struct PushConsts {
    uint TextureSlot;
    uint SamplerSlot;
};
[[vk::push_constant]] PushConsts pc;

struct VSOut {
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID : SV_VertexID) {
    // Single-triangle fullscreen trick — UV in [0,2], pos in [-1,3]; one
    // triangle covers the viewport. No index buffer, no vertex buffer.
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    VSOut Out;
    Out.UV = UV;
    Out.Position = float4(UV * 2.0f - 1.0f, 0.0f, 1.0f);
    return Out;
}

[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {
    return GetTexture2D(pc.TextureSlot).Sample(GetSampler(pc.SamplerSlot), In.UV);
}
