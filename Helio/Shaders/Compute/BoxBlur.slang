// BoxBlur.slang — separable-ish box blur over a sampled-image input.
//
// A common post-process compute pattern: read a SAMPLED texture, average its
// neighborhood, write the result to a STORAGE image. Demonstrates:
//   - multi-input compute (texture + sampler + params)
//   - bounds-aware neighborhood sampling
//   - separating compute thread layout from image layout (any thread count
//     divides into the image with `Dispatch2D`).
//
// For a real blur, do two passes (horizontal + vertical) — but a single-pass
// box blur is enough as a reference of the pattern.

import Bindless;

struct PC {
    uint  InputSlot;        // SampledSlot of the source texture
    uint  OutputSlot;       // StorageSlot of the destination texture (same size)
    uint  SamplerSlot;      // typically kSamplerLinearClamp
    int   Radius;           // odd half-extent: radius=2 means 5x5 kernel
    float Width;
    float Height;
};
[[vk::push_constant]] PC pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void CSMain(uint3 TID : SV_DispatchThreadID) {
    RWTexture2D<float4> Out = GetStorageImage(pc.OutputSlot);
    if (TID.x >= uint(pc.Width) || TID.y >= uint(pc.Height)) return;

    Texture2D<float4> Src = GetTexture2D(pc.InputSlot);
    SamplerState S = GetSampler(pc.SamplerSlot);
    float2 PixelSize = float2(1.0, 1.0) / float2(pc.Width, pc.Height);
    float2 UV = (float2(TID.xy) + 0.5) * PixelSize;

    float4 Sum = float4(0, 0, 0, 0);
    float  Weight = 0.0;

    // Brute-force NxN box. For Radius=2 → 5x5 = 25 samples per pixel.
    for (int dy = -pc.Radius; dy <= pc.Radius; ++dy) {
        for (int dx = -pc.Radius; dx <= pc.Radius; ++dx) {
            float2 SampleUV = UV + float2(dx, dy) * PixelSize;
            Sum += Src.SampleLevel(S, SampleUV, 0);
            Weight += 1.0;
        }
    }

    Out[TID.xy] = Sum / Weight;
}
