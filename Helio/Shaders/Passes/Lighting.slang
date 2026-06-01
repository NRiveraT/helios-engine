import Bindless;
import Fullscreen;

struct PC { uint AlbedoSlot, NormalSlot, EmissiveSlot, SamplerSlot; };
[[vk::push_constant]] PC pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint VertexID : SV_VertexID) { return FullscreenVS(VertexID); }

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
    return GetTexture2D(pc.NormalSlot).Sample(GetSampler(pc.SamplerSlot), In.UV);
}
