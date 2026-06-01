// Gradient.slang — simplest possible compute shader.
//
// Writes a UV gradient (R = U, G = V, B = 0.5) into a storage image.
// Use this to verify the compute → storage-image → fullscreen-blit chain
// before writing anything fancier.

import Bindless;

struct PC {
    uint OutputSlot;
};
[[vk::push_constant]] PC pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void CSMain(uint3 TID : SV_DispatchThreadID) {
    RWTexture2D<float4> Out = GetStorageImage(pc.OutputSlot);

    uint W, H;
    Out.GetDimensions(W, H);
    if (TID.x >= W || TID.y >= H) return;

    float2 UV = float2(TID.xy) / float2(W, H);
    Out[TID.xy] = float4(UV, 0.5, 1.0);
}
