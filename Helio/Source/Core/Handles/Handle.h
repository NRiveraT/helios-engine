/// @file Handle.h
/// @brief Strongly-typed opaque handles + a slot freelist.
///
/// Handles let us pass GPU resources around as 64-bit values without exposing
/// Vulkan internals. The phantom Tag type prevents mixing e.g. TextureHandle
/// with BufferHandle.
///
/// Usage:
/// @code
///     struct TextureTag {};
///     using TextureHandle = ::helio::core::Handle<TextureTag>;
///
///     TextureHandle Tex = Rhi.CreateTexture(...);
///     if (Tex.IsValid()) Rhi.DestroyTexture(Tex);
/// @endcode
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace helio::core {

template <typename Tag>
struct Handle {
    using IndexT = uint32_t;
    using GenT = uint32_t;

    static constexpr IndexT InvalidIndex = (std::numeric_limits<IndexT>::max)();

    IndexT Index{InvalidIndex};
    GenT Generation{0};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
    constexpr bool operator==(const Handle&) const noexcept = default;
};

/// Index freelist used by bindless slot allocators and resource pools.
/// LIFO: most recently freed slot is returned first (keeps hot working set warm).
class Freelist {
public:
    explicit Freelist(uint32_t Count);

    /// Allocate the next free slot. Returns Handle::InvalidIndex if exhausted.
    [[nodiscard]] uint32_t Allocate();

    /// Return a slot to the pool. Must have been previously allocated.
    void Free(uint32_t Slot);

    [[nodiscard]] uint32_t Capacity() const noexcept { return m_capacity; }
    [[nodiscard]] uint32_t FreeCount() const noexcept { return static_cast<uint32_t>(m_free.size()); }

private:
    uint32_t m_capacity;
    std::vector<uint32_t> m_free;
};

} // namespace helio::core
