/// @file VulkanDeletionQueue.h
/// @brief Defer destruction until the GPU is done with the resource.
///
/// `Push()` queues a destructor lambda against the *current* frame slot.
/// `OnFrameRetired(slot)` runs all lambdas previously queued at that slot.
/// VulkanContext calls `OnFrameRetired(m_frameIndex)` at the start of each
/// frame, right after waiting on the frame's InFlight fence — which is the
/// guarantee that everything previously submitted on that slot has finished.
#pragma once

#include <array>
#include <functional>
#include <vector>

namespace helio::rhi::vulkan {

inline constexpr uint32_t kDeletionSlots = 2; // == FramesInFlight

class VulkanDeletionQueue {
public:
    using Deleter = std::function<void()>;

    /// Queue a destructor against frame slot `Slot`. It will run the next
    /// time `OnFrameRetired(Slot)` is invoked (i.e. after the GPU consumes
    /// any frames currently submitted with that slot).
    void Push(uint32_t Slot, Deleter Fn);

    /// Run all deleters queued at `Slot`, then clear.
    void OnFrameRetired(uint32_t Slot);

    /// Flush everything regardless of slot — call at shutdown after WaitIdle.
    void FlushAll();

private:
    std::array<std::vector<Deleter>, kDeletionSlots> m_pending{};
};

} // namespace helio::rhi::vulkan
