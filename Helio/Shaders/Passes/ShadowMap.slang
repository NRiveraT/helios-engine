// ShadowMap.slang — depth-only pass rendering casters from the sun's view.
//
// The light's view-projection comes from FrameConstants (the SAME matrix the
// mesh pass samples with — one source of truth, so UV orientation and depth
// convention agree by construction). Push constants mirror
// `helio::resource::ShadowMapPushConsts` (16 bytes).
//
// Conventions match every other 3D pass: reverse-Z (clear 0, CompareOp
// Greater), Y-negated ortho projection, FrontFace::Clockwise.

import Vertex;
import Frame;

struct PC {
    uint FrameSlot;
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
    Vertex V = LoadVertex(pc.VertexBufferSlot, VertexID);
    float4x4 Xform = LoadInstanceTransform(pc.InstanceBufferSlot,
                                           pc.InstanceBase + InstanceID);

    FrameConstants Frame = LoadFrameConstants(pc.FrameSlot);

    VSOut Out;
    float4 WorldPos = mul(Xform, float4(V.Pos, 1.0));
    Out.Position = mul(Frame.LightViewProj, WorldPos);
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
