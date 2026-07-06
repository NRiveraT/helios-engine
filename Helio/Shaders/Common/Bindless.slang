// Bindless.slang
//
// Helio's global bindless layout. Bindings MUST match
// `Helio/Source/RHI/Vulkan/VulkanBindless.h`:
//   set 0, binding 0: sampled images   (unbounded array)
//   set 0, binding 1: storage images   (unbounded array)
//   set 0, binding 2: storage buffers  (unbounded array)
//   set 0, binding 3: samplers         (5 static slots)
//   set 0, binding 4: acceleration structure (TLAS, 1 slot)
//
// Use the Get*() helpers below from any shader; they wrap
// `NonUniformResourceIndex` for divergent indexing.
//
// Binding 3 is declared TWICE — once as `SamplerState`, once as
// `SamplerComparisonState`. Both alias the same descriptor array (Vulkan
// descriptor type SAMPLER either way); the comparison view is how shadow
// code reaches the hardware-PCF sampler at slot `kSamplerShadowLinear`.

[[vk::binding(0, 0)]] Texture2D<float4>   g_BindlessTextures2D[];
[[vk::binding(1, 0)]] RWTexture2D<float4> g_BindlessStorageImages[];
[[vk::binding(2, 0)]] ByteAddressBuffer   g_BindlessStorageBuffers[];
[[vk::binding(3, 0)]] SamplerState        g_BindlessSamplers[5];
[[vk::binding(3, 0)]] SamplerComparisonState g_BindlessSamplersCmp[5];
[[vk::binding(4, 0)]] RaytracingAccelerationStructure g_BindlessTLAS;

Texture2D<float4> GetTexture2D(uint slot) {
    return g_BindlessTextures2D[NonUniformResourceIndex(slot)];
}

RWTexture2D<float4> GetStorageImage(uint slot) {
    return g_BindlessStorageImages[NonUniformResourceIndex(slot)];
}

ByteAddressBuffer GetStorageBuffer(uint slot) {
    return g_BindlessStorageBuffers[NonUniformResourceIndex(slot)];
}

SamplerState GetSampler(uint slot) {
    return g_BindlessSamplers[slot];
}

SamplerComparisonState GetSamplerCmp(uint slot) {
    return g_BindlessSamplersCmp[slot];
}

RaytracingAccelerationStructure GetTLAS() {
    return g_BindlessTLAS;
}

// Static sampler enum (matches VulkanBindless::SamplerSlot).
static const uint kSamplerLinearClamp  = 0;
static const uint kSamplerLinearWrap   = 1;
static const uint kSamplerPointClamp   = 2;
static const uint kSamplerPointWrap    = 3;
/// Shadow comparison sampler (GREATER_OR_EQUAL, reverse-Z, hardware PCF).
/// Only meaningful through `GetSamplerCmp`.
static const uint kSamplerShadowLinear = 4;
