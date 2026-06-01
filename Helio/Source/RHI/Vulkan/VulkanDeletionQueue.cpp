#include "VulkanDeletionQueue.h"

#include <Core/Assert/Assert.h>

namespace helio::rhi::vulkan {

void VulkanDeletionQueue::Push(uint32_t Slot, Deleter Fn) {
    HELIO_ASSERT(Slot < kDeletionSlots);
    m_pending[Slot].push_back(std::move(Fn));
}

void VulkanDeletionQueue::OnFrameRetired(uint32_t Slot) {
    HELIO_ASSERT(Slot < kDeletionSlots);
    auto& Bucket = m_pending[Slot];
    for (auto& Fn : Bucket) {
        Fn();
    }
    Bucket.clear();
}

void VulkanDeletionQueue::FlushAll() {
    for (auto& Bucket : m_pending) {
        for (auto& Fn : Bucket) Fn();
        Bucket.clear();
    }
}

} // namespace helio::rhi::vulkan
