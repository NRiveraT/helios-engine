// CommandList method bodies — dispatch off the public `CommandList`'s opaque
// m_impl pointer (a VulkanCommandListImpl*) and call into Vulkan directly.

#include <RHI/Public/CommandList.h>
#include "VulkanCommandList.h"
#include "VulkanContext.h"
#include "VulkanBindless.h"
#include "VulkanCheck.h"

#include <Core/Assert/Assert.h>

namespace helio::rhi {

namespace {

using vulkan::VulkanCommandListImpl;

VulkanCommandListImpl* I(CommandList& C) noexcept {
    return static_cast<VulkanCommandListImpl*>(C.Impl());
}

} // namespace

namespace vulkan {

void ResetCommandList(CommandList& Public, VulkanCommandListImpl& Impl) {
    Impl.Bound = {};
    Impl.InRendering = false;
    Public.SetImpl(&Impl);
}

} // namespace vulkan

void CommandList::BeginRenderingToSwapchain(float R, float G, float B, float A) {
    auto* Impl = I(*this);
    HELIO_CHECK(Impl && !Impl->InRendering);
    Impl->Ctx->BeginRenderingToSwapchainInternal(Impl, R, G, B, A);
    Impl->InRendering = true;
}

void CommandList::BeginRendering(const ColorAttachment* Colors, uint32_t NumColors, const DepthAttachment* Depth) {
    auto* Impl = I(*this);
    HELIO_CHECK(Impl && !Impl->InRendering);
    Impl->Ctx->BeginRenderingToTexturesInternal(Impl, Colors, NumColors, Depth);
    Impl->InRendering = true;
}

void CommandList::BeginRendering(std::initializer_list<ColorAttachment> Colors, const DepthAttachment* Depth) {
    BeginRendering(Colors.begin(), static_cast<uint32_t>(Colors.size()), Depth);
}

void CommandList::EndRendering() {
    auto* Impl = I(*this);
    if (!Impl || !Impl->InRendering) return;
    vkCmdEndRendering(Impl->Cmd);
    Impl->InRendering = false;
}

void CommandList::TransitionForSampling(TextureHandle Tex) {
    auto* Impl = I(*this);
    HELIO_CHECK(Impl && !Impl->InRendering);
    Impl->Ctx->TransitionForSamplingInternal(Impl, Tex);
}

void CommandList::Bind(PipelineHandle H) {
    auto* Impl = I(*this);
    auto* P = Impl->Ctx->LookupPipeline(H);
    HELIO_CHECK(P);
    Impl->Bound = *P;
    vkCmdBindPipeline(Impl->Cmd, P->BindPoint, P->Pipeline);

    VkDescriptorSet Set = Impl->Ctx->GetBindless()->GetSet();
    vkCmdBindDescriptorSets(Impl->Cmd, P->BindPoint, P->Layout, 0, 1, &Set, 0, nullptr);
}

void CommandList::Push(const void* Data, uint32_t Size) {
    auto* Impl = I(*this);
    HELIO_CHECK(Impl->Bound.Layout);
    HELIO_CHECK(Size <= Impl->Bound.PushConstantBytes);
    vkCmdPushConstants(Impl->Cmd, Impl->Bound.Layout, VK_SHADER_STAGE_ALL, 0, Size, Data);
}

void CommandList::SetViewportFull() {
    auto* Impl = I(*this);
    Impl->Ctx->SetViewportFullInternal(Impl);
}

void CommandList::Draw(uint32_t VertexCount, uint32_t InstanceCount,
                       uint32_t FirstVertex, uint32_t FirstInstance) {
    auto* Impl = I(*this);
    vkCmdDraw(Impl->Cmd, VertexCount, InstanceCount, FirstVertex, FirstInstance);
}

void CommandList::Dispatch(uint32_t GroupsX, uint32_t GroupsY, uint32_t GroupsZ) {
    auto* Impl = I(*this);
    vkCmdDispatch(Impl->Cmd, GroupsX, GroupsY, GroupsZ);
}

void CommandList::Dispatch2D(uint32_t SizeX, uint32_t SizeY, uint32_t GroupX, uint32_t GroupY) {
    uint32_t Gx = (SizeX + GroupX - 1) / GroupX;
    uint32_t Gy = (SizeY + GroupY - 1) / GroupY;
    Dispatch(Gx, Gy, 1);
}

void CommandList::DrawIndexed(uint32_t IndexCount, uint32_t InstanceCount,
                              uint32_t FirstIndex, int32_t VertexOffset, uint32_t FirstInstance) {
    auto* Impl = I(*this);
    vkCmdDrawIndexed(Impl->Cmd, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
}

void CommandList::BindVertexBuffer(BufferHandle Buf, uint32_t Binding, uint64_t Offset) {
    auto* Impl = I(*this);
    Impl->Ctx->BindVertexBufferInternal(Impl, Buf, Binding, Offset);
}

void CommandList::BindIndexBuffer(BufferHandle Buf, IndexType Type, uint64_t Offset) {
    auto* Impl = I(*this);
    VkIndexType VkT = (Type == IndexType::U16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    Impl->Ctx->BindIndexBufferInternal(Impl, Buf, VkT, Offset);
}

void CommandList::BlitImage(TextureHandle Src, TextureHandle Dst, BlitFilter Filter) {
    auto* Impl = I(*this);
    HELIO_CHECK(!Impl->InRendering);  // must close any active render scope first
    VkFilter F = (Filter == BlitFilter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    Impl->Ctx->BlitImageInternal(Impl, Src, Dst, F);
}

void CommandList::CopyImage(TextureHandle Src, TextureHandle Dst) {
    auto* Impl = I(*this);
    HELIO_CHECK(!Impl->InRendering);
    Impl->Ctx->CopyImageInternal(Impl, Src, Dst);
}

void CommandList::BlitToSwapchain(TextureHandle Src, BlitFilter Filter) {
    auto* Impl = I(*this);
    HELIO_CHECK(!Impl->InRendering);
    VkFilter F = (Filter == BlitFilter::Linear) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    Impl->Ctx->BlitToSwapchainInternal(Impl, Src, F);
}

void CommandList::TransitionForStorageWrite(TextureHandle Tex) {
    auto* Impl = I(*this);
    HELIO_CHECK(!Impl->InRendering);
    Impl->Ctx->TransitionForStorageWriteInternal(Impl, Tex);
}

} // namespace helio::rhi
