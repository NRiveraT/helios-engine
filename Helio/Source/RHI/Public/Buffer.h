/// @file Buffer.h
/// @brief Public Buffer handle + descriptor for the bindless RHI.
///
/// Buffers tagged with `BufferUsage::Storage` get a global bindless slot
/// assigned at creation time — the `BindlessSlot` field on `BufferHandle`.
/// Pass that slot to a shader via push constants and access via
/// `Shaders/Common/Bindless.slang` helpers.
#pragma once

#include <cstdint>
#include <type_traits>

namespace helio::rhi {

enum class BufferUsage : uint32_t {
    None                  = 0,
    Vertex                = 1u << 0,
    Index                 = 1u << 1,
    /// Storage buffer — gets a bindless slot.
    Storage               = 1u << 2,
    Uniform               = 1u << 3,
    TransferSrc           = 1u << 4,
    TransferDst           = 1u << 5,
    Indirect              = 1u << 6,
    ShaderDeviceAddress   = 1u << 7,
    AccelStructureBuild   = 1u << 8,
    AccelStructureStorage = 1u << 9,
    SBT                   = 1u << 10,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage A, BufferUsage B) noexcept {
    return static_cast<BufferUsage>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
}
[[nodiscard]] constexpr BufferUsage operator&(BufferUsage A, BufferUsage B) noexcept {
    return static_cast<BufferUsage>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
}
[[nodiscard]] constexpr bool HasFlag(BufferUsage Set, BufferUsage Flag) noexcept {
    return (static_cast<uint32_t>(Set) & static_cast<uint32_t>(Flag)) != 0;
}

enum class MemoryUsage : uint8_t {
    /// GPU-only. Best perf, requires upload via staging.
    DeviceLocal,
    /// CPU-writable (sequential), GPU-readable. Use for staging + dynamic UBOs.
    HostUpload,
    /// CPU-readable (random), GPU-writable. Readback path.
    HostReadback,
};

struct BufferDesc {
    uint64_t Size{0};
    BufferUsage Usage{BufferUsage::Storage};
    MemoryUsage Memory{MemoryUsage::DeviceLocal};
    /// Debug label visible in RenderDoc / validation messages. Optional.
    const char* DebugName{nullptr};
    /// Optional one-shot upload right after creation. Staged via the
    /// device's transfer helper. NULL if you'll upload later.
    const void* InitialData{nullptr};
    uint64_t InitialDataSize{0};
};

struct BufferHandle {
    /// Opaque non-zero ID for valid handles. Lookup is O(1) inside the RHI.
    uint64_t Id{0};
    /// Global bindless slot for storage-buffer access in shaders.
    /// `UINT32_MAX` if the buffer was not created with `Storage` usage.
    uint32_t BindlessSlot{0xFFFFFFFFu};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Id != 0; }
};

} // namespace helio::rhi
