/// @file RingUploadBuffer.h
/// @brief Per-frame host-upload storage buffer with safe cross-frame writes.
///
/// Why this exists: `MemoryUsage::HostUpload` writes go straight to a mapped
/// pointer with no fence between CPU and GPU. If you write to the same buffer
/// every frame and the GPU is still reading the previous frame's data, you
/// race — visible as flickering, stale transforms, or torn instance data.
///
/// `RingUploadBuffer` owns one buffer per frame-in-flight slot. `Current()`
/// returns the slot the GPU will read this frame; the next frame rotates to
/// a different slot whose GPU work has already retired. Sized for the worst
/// case so you never have to resize mid-frame.
///
/// Use cases: per-frame instance data, glyph queues, debug-draw vertex lists,
/// any structured-buffer payload that's written by the CPU and read by the
/// GPU in the same frame.
#pragma once

#include <RHI/Public/Buffer.h>

#include <cstdint>
#include <vector>

namespace helio::rhi {

class Device;

class RingUploadBuffer {
public:
    /// `SizeBytes` is the per-slot capacity (you get FramesInFlight × this
    /// total). `DebugName` is shown in RenderDoc / VMA; the slot index is
    /// appended automatically.
    RingUploadBuffer(Device& Dev, uint64_t SizeBytes, const char* DebugName = nullptr);
    ~RingUploadBuffer();

    RingUploadBuffer(const RingUploadBuffer&) = delete;
    RingUploadBuffer& operator=(const RingUploadBuffer&) = delete;

    /// The handle for the slot the CPU should write to and the GPU will read
    /// this frame. Re-query each frame — the bindless slot rotates with the
    /// device's current frame index.
    [[nodiscard]] BufferHandle Current() const;

    /// memcpy `Data` (`Size` bytes) into the current slot at `Offset`. Wraps
    /// `Device::UploadToBuffer`. Caller is responsible for staying within
    /// the per-slot `SizeBytes` capacity.
    void Write(uint64_t Offset, const void* Data, uint64_t Size);

    /// Per-slot capacity in bytes. Total VRAM footprint is `Size() * FramesInFlight()`.
    [[nodiscard]] uint64_t Size() const noexcept { return m_size; }

private:
    Device*                   m_dev;
    uint64_t                  m_size;
    std::vector<BufferHandle> m_slots;
};

} // namespace helio::rhi
