import Fullscreen; //  pulls in FullscreenVS / FullscreenVSOut. FullscreenVS(id) turns the vertex index (0,1,2) into one big triangle covering the screen, and hands the fragment stage In.UV in [0,1] — pixel (0,0) top-left is UV (0,0)
import Bindless; // gives you GetTexture2D(slot) and GetSampler(slot). In a bindless engine the shader doesn't "bind" a texture — it indexes the global array by an integer slot you pass in

struct DebugPushConst {
  uint Mode; // 1 = depth, 2 = world normal,
  uint SourceSlot;
  float NearZ;
  float DebugFar;
};
[[vk::push_constant]]
DebugPushConst pc;

[shader("vertex")]
FullscreenVSOut VSMain(uint id: SV_VertexID) { return FullscreenVS(id); }

[shader("fragment")]
float4 PSMain(FullscreenVSOut In) : SV_Target {
  if (pc.Mode == 1) {
  //GetTexture2D(pc.SourceSlot).Sample(GetSampler(kSamplerLinearClamp), In.UV).rgb;
    float d = GetTexture2D(pc.SourceSlot).Sample(GetSampler(kSamplerPointClamp), In.UV).r;
    if (d <= 0)
      return float4(0, 0, 0, 1);

    float zView = pc.NearZ / d;
    float g = 1.0 - saturate(zView / pc.DebugFar);
    return float4(g, g, g, 1.0);
  }
  
  if (pc.Mode == 2)
  {
  	float3 n = GetTexture2D(pc.SourceSlot).Sample(GetSampler(kSamplerPointClamp), In.UV).rgb;
  	return float4(n * 0.5 + 0.5, 1.0);
  }

  return float4(1, 0, 1, 1);
}
