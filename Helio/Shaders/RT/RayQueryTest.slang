// RayQueryTest.slang — verification compute pass for the TLAS plumbing.
//
// For each pixel, fires a ray from `(NDC.x, NDC.y, -1)` in +Z direction at
// the world-origin verification triangle (`BuildVerificationTLAS()` puts a
// unit XY triangle at z=0). Writes white on hit, black on miss.
//
// Bind a Storage texture at the slot passed in `pc.OutputSlot` (must have
// `TextureUsage::Storage`). The triangle is at z=0 facing +Z; rays going +Z
// will hit if their (x,y) lands inside the triangle.

import Bindless;

struct PC {
    uint OutputSlot;
};
[[vk::push_constant]] PC pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void CSMain(uint3 ThreadID : SV_DispatchThreadID) {
    RWTexture2D<float4> Out = GetStorageImage(pc.OutputSlot);

    uint Width, Height;
    Out.GetDimensions(Width, Height);
    if (ThreadID.x >= Width || ThreadID.y >= Height) return;

    // Pixel center in [0,1], then NDC in [-1, +1].
    float2 UV = (float2(ThreadID.xy) + 0.5) / float2(Width, Height);
    float2 NDC = UV * 2.0 - 1.0;

    // Ray from in front of the triangle (z=-1) shooting toward +Z.
    RayDesc Ray;
    Ray.Origin    = float3(NDC.x, NDC.y, -1.0);
    Ray.Direction = float3(0.0, 0.0, 1.0);
    Ray.TMin      = 0.001;
    Ray.TMax      = 100.0;

    RayQuery<RAY_FLAG_NONE> Q;
    Q.TraceRayInline(GetTLAS(), 0u, 0xFFu, Ray);
    Q.Proceed();

    float4 Color = (Q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        ? float4(1.0, 1.0, 1.0, 1.0)    // hit  → white
        : float4(0.0, 0.0, 0.0, 1.0);   // miss → black

    Out[ThreadID.xy] = Color;
}
