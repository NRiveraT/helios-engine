// PostProcess.slang - fullscreen post process triangle
// M0 - Just reads scene color.

import Fullscreen; //  pulls in FullscreenVS / FullscreenVSOut. FullscreenVS(id) turns the vertex index (0,1,2) into one big triangle covering the screen, and hands the fragment stage In.UV in [0,1] — pixel (0,0) top-left is UV (0,0)
import Bindless; // gives you GetTexture2D(slot) and GetSampler(slot). In a bindless engine the shader doesn't "bind" a texture — it indexes the global array by an integer slot you pass in

struct PostProcessPush {
	uint SourceSlot; // Bindless slot of texture to READ
}

[[vk::push_constant]]
PostProcessPush pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint id: SV_VertexID) {
	return FullscreenVS(id); // Wires straight to a fullscreen triangle
}

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
	float3 color = GetTexture2D(pc.SourceSlot).Sample(GetSampler(kSamplerLinearClamp), In.UV).rgb;
	
	float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
	return float4(color, 1.0);
}
