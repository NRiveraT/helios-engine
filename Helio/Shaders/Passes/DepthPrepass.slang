import Vertex;
import Frame;
import Bindless;

struct PC {
  uint FrameSlot;
  uint VertexBufferSlot;
  uint InstanceBufferSlot;
  uint InstanceBase;
  uint SamplerSlot;
};
[[vk::push_constant]]
PC pc;

struct VSOut {
  float4 Position : SV_Position;
  float3 NormalWS : NORMAL0;
};

[shader("vertex")]
VSOut VSMain(uint VertexID: SV_VertexID, uint InstanceID: SV_InstanceID) {
	Vertex V = LoadVertex(pc.VertexBufferSlot, VertexID);
	float4x4 Xform = LoadInstanceTransform(pc.InstanceBufferSlot,
										 pc.InstanceBase + InstanceID);
	
	FrameConstants Frame = LoadFrameConstants(pc.FrameSlot);
	float3x3 M = (float3x3)Xform;
	float3x3 Cof = float3x3(cross(M[1], M[2]), cross(M[2], M[0]), cross(M[0], M[1]));
	
	VSOut Out;
	float4 WorldPos = mul(Xform, float4(V.Pos, 1.0));
	Out.Position = mul(Frame.ViewProj, WorldPos);
	Out.NormalWS = normalize(mul(Cof, V.Normal));
	return Out;
}

// Depth-only pipeline: pipeline's ColorAttachmentCount is 0, so the fragment
// shader MUST NOT declare any color outputs. Returning void with no SV_Target
// matches that. Depth is written automatically from the rasterizer's
// interpolated SV_Position.z. Per Vulkan spec on fragment-output interfaces,
// mismatching outputs ↔ attachments yields undefined behavior — which on
// some drivers manifests as NaN positions in the VS output stage.
[shader("fragment")]
float4 PSMain(VSOut In) : SV_Target {	
	return float4(normalize(In.NormalWS), 1.0);
}
