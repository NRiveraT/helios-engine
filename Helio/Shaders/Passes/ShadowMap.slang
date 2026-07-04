import Bindless;
import Vertex;

import Fullscreen;

struct PC {
  float4x4 LightViewProj;
  uint VertexBufferSlot;
  uint InstanceBufferSlot;
  uint InstanceBase;
};
[[vk::push_constant]]
PC pc;

struct VSOut {
  float4 Position : SV_Position;
};

[shader("vertex")]
VSOut VSMain(uint VertexID: SV_VertexID, uint InstanceID: SV_InstanceID) {
  VSOut Out;

  Vertex V = LoadVertex(pc.VertexBufferSlot, VertexID);
  float4x4 Xform = LoadInstanceTransform(pc.InstanceBufferSlot,
                                         pc.InstanceBase + InstanceID);

  float4 WorldPos = mul(Xform, float4(V.Pos, 1.0f));
  Out.Position = mul(pc.LightViewProj, WorldPos);

  return Out;
}

// Depth-only pipeline: pipeline's ColorAttachmentCount is 0, so the fragment
// shader MUST NOT declare any color outputs. Returning void with no SV_Target
// matches that. Depth is written automatically from the rasterizer's
// interpolated SV_Position.z. Per Vulkan spec on fragment-output interfaces,
// mismatching outputs ↔ attachments yields undefined behavior — which on
// some drivers manifests as NaN positions in the VS output stage.
[shader("fragment")]
void PSMain(VSOut In) { }
