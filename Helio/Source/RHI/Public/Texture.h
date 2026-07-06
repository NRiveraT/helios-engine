/// @file Texture.h
/// @brief Public Texture handle + descriptor for the bindless RHI.
///
/// A texture can simultaneously occupy:
/// - one bindless `sampled image` slot (if `Usage::Sampled`)
/// - one bindless `storage image` slot (if `Usage::Storage`)
/// Common pattern: scene textures are Sampled; compute output targets are Storage.
#pragma once

#include <RHI/Public/Formats.h>

#include <cstdint>

namespace helio::rhi {

enum class TextureUsage : uint32_t {
    None                   = 0,
    /// Read in a shader via a sampled-image bindless slot.
    Sampled                = 1u << 0,
    /// Written in a shader via a storage-image bindless slot.
    Storage                = 1u << 1,
    ColorAttachment        = 1u << 2,
    DepthStencilAttachment = 1u << 3,
    TransferSrc            = 1u << 4,
    TransferDst            = 1u << 5,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage A, TextureUsage B) noexcept {
    return static_cast<TextureUsage>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
}
[[nodiscard]] constexpr TextureUsage operator&(TextureUsage A, TextureUsage B) noexcept {
    return static_cast<TextureUsage>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
}
[[nodiscard]] constexpr bool HasFlag(TextureUsage Set, TextureUsage Flag) noexcept {
    return (static_cast<uint32_t>(Set) & static_cast<uint32_t>(Flag)) != 0;
}

struct TextureDesc {
    uint32_t Width{1};
    uint32_t Height{1};
    uint32_t Depth{1};
    uint32_t MipLevels{1};
    uint32_t ArrayLayers{1};
    Format Fmt{Format::RGBA8_UNORM};
    TextureUsage Usage{TextureUsage::Sampled};
    const char* DebugName{nullptr};
    /// Optional initial mip 0 / layer 0 upload, tightly packed.
    const void* InitialData{nullptr};
    uint64_t InitialDataSize{0};
    /// Generate a full mip chain from `InitialData` at create time (successive
    /// linear-filtered blits). Overrides `MipLevels` with the full count for
    /// the given dimensions and implicitly adds TRANSFER_SRC usage. Requires
    /// `InitialData`, a 2D non-array color texture. Off by default; turn on for
    /// sampled material textures so they don't shimmer/alias at distance.
    bool GenerateMips{false};
};

struct TextureHandle {
    uint64_t Id{0};
    /// Bindless slot for sampling (read). `UINT32_MAX` if not Sampled.
    uint32_t SampledSlot{0xFFFFFFFFu};
    /// Bindless slot for storage (write). `UINT32_MAX` if not Storage.
    uint32_t StorageSlot{0xFFFFFFFFu};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Id != 0; }
};

} // namespace helio::rhi
