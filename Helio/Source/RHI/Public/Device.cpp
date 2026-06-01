#include "Device.h"
#include "../Vulkan/VulkanContext.h"

namespace helio::rhi {

struct Device::Impl {
    vulkan::VulkanContext Ctx;
    explicit Impl(const DeviceConfig& Config) : Ctx(Config) {}
};

Device::Device(const DeviceConfig& Config)
    : m_impl(std::make_unique<Impl>(Config)) {}

Device::~Device() = default;

CommandList* Device::BeginFrame() { return m_impl->Ctx.BeginFrame(); }
void Device::EndFrame() { m_impl->Ctx.EndFrame(); }
void Device::WaitIdle() { m_impl->Ctx.WaitIdle(); }
void Device::Resize(int Width, int Height) { m_impl->Ctx.Resize(Width, Height); }

BufferHandle Device::CreateBuffer(const BufferDesc& Desc) { return m_impl->Ctx.CreateBuffer(Desc); }
void Device::DestroyBuffer(BufferHandle H) { m_impl->Ctx.DestroyBuffer(H); }
void Device::UploadToBuffer(BufferHandle H, uint64_t Offset, const void* Data, uint64_t Size) {
    m_impl->Ctx.UploadToBuffer(H, Offset, Data, Size);
}
TextureHandle Device::CreateTexture(const TextureDesc& Desc) { return m_impl->Ctx.CreateTexture(Desc); }
void Device::DestroyTexture(TextureHandle H) { m_impl->Ctx.DestroyTexture(H); }
Device::BindlessUsage Device::GetBindlessUsage() const { return m_impl->Ctx.GetBindlessUsage(); }

bool Device::HasRayTracing() const { return m_impl->Ctx.HasRayTracing(); }
RayTracingProperties Device::GetRayTracingProperties() const { return m_impl->Ctx.GetRayTracingProperties(); }
double Device::LastFrameGpuMs() const { return m_impl->Ctx.GetLastFrameGpuMs(); }
uint32_t Device::FramesInFlight() const noexcept { return vulkan::FramesInFlight; }
uint32_t Device::CurrentFrameIndex() const noexcept { return m_impl->Ctx.GetCurrentFrameIndex(); }

PipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& Desc) { return m_impl->Ctx.CreateGraphicsPipeline(Desc); }
PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc& Desc) { return m_impl->Ctx.CreateComputePipeline(Desc); }
void Device::DestroyPipeline(PipelineHandle H) { m_impl->Ctx.DestroyPipeline(H); }

BLASHandle Device::BuildBLAS(const BLASDesc& Desc) { return m_impl->Ctx.BuildBLAS(Desc); }
void Device::DestroyBLAS(BLASHandle H) { m_impl->Ctx.DestroyBLAS(H); }
TLASHandle Device::BuildTLAS(const TLASDesc& Desc) { return m_impl->Ctx.BuildTLAS(Desc); }
void Device::DestroyTLAS(TLASHandle H) { m_impl->Ctx.DestroyTLAS(H); }
void Device::SetActiveTLAS(TLASHandle H) { m_impl->Ctx.SetActiveTLAS(H); }
TLASHandle Device::BuildVerificationTLAS() { return m_impl->Ctx.BuildVerificationTLAS(); }

} // namespace helio::rhi
