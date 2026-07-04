import Bindless;
import Fullscreen;

struct PC { uint AlbedoSlot, SamplerSlot; };
[[vk::push_constant]] PC pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint VertexID : SV_VertexID) { return FullscreenVS(VertexID); }

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
	float3 ColorOut = GetTexture2D(pc.AlbedoSlot).Sample(GetSampler(pc.SamplerSlot), In.UV).rgb;
	ColorOut /= 100.0;
	
    return float4(ColorOut, 1.0);
}
