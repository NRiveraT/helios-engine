// Fullscreen.slang — shared fullscreen-triangle helper.
//
// Vulkan/D3D/Metal all require a vertex stage on every graphics pipeline.
// For shaders that only care about fragment work (fullscreen blits, deferred
// lighting passes, post-process), wire your pipeline up like this and call
// `Cmd->Draw(3)`:
//
//     import Fullscreen;
//
//     [shader("vertex")]
//     FullscreenVSOut VSMain(uint id : SV_VertexID) {
//         return FullscreenVS(id);
//     }
//
//     [shader("fragment")]
//     float4 PSMain(FullscreenVSOut In) : SV_Target {
//         /* sample / shade using In.UV in [0,1] */
//     }
//
// The geometry is a single oversized triangle with UVs that span [0,2] in
// raster space; the half outside the viewport gets clipped, leaving the screen
// fully covered with UV in [0,1] — cheaper and smoother than a 2-triangle quad.

public struct FullscreenVSOut {
    public float4 Position : SV_Position;
    public float2 UV       : TEXCOORD0;
};

public FullscreenVSOut FullscreenVS(uint VertexID) {
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    FullscreenVSOut Out;
    Out.UV       = UV;
    Out.Position = float4(UV * 2.0 - 1.0, 0.0, 1.0);
    return Out;
}
