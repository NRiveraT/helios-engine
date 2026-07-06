/// @file TextureCache.h
/// @brief Owns GPU textures decoded from imported images (glTF materials).
///
/// A `TextureCache` decodes compressed image bytes (PNG / JPG / … via stb),
/// uploads them as mipmapped sampled textures through the bindless RHI, and
/// hands back the bindless slot to store on a `Material`. It owns every
/// texture it creates and frees them on destruction — one cache per
/// `MeshSystem`, torn down with it.
///
/// De-duplication (a glTF image referenced by many materials, or the same
/// image needed as both sRGB and linear) is the importer's job — it holds the
/// image-index → slot map for one import and calls `LoadImage` once per unique
/// (image, colorspace).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <RHI/Public/Texture.h>

namespace helio::rhi { class Device; }

namespace helio::resource {

/// A CPU-side decoded RGBA8 image. Produced by `TextureCache::Decode` (which is
/// thread-safe and touches no GPU state, so many can be decoded in parallel)
/// and consumed by `TextureCache::Upload` on the main thread.
struct DecodedImage {
    std::vector<unsigned char> Pixels; // tightly-packed RGBA8, W*H*4 bytes
    int W = 0;
    int H = 0;
    [[nodiscard]] bool IsValid() const noexcept { return W > 0 && H > 0 && !Pixels.empty(); }
};

class TextureCache {
public:
    explicit TextureCache(rhi::Device& Dev);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    /// Decode `Size` bytes of a compressed image (PNG/JPG/… via stb) to RGBA8.
    /// THREAD-SAFE and GPU-free — call from any thread / worker, e.g. to decode
    /// a whole model's images in parallel before uploading. Returns an invalid
    /// `DecodedImage` on failure (logs the reason).
    [[nodiscard]] static DecodedImage Decode(const void* Bytes, size_t Size,
                                             const char* DebugName);

    /// Upload an already-decoded image as a mipmapped sampled texture and
    /// return its bindless SampledSlot (`UINT32_MAX` on failure). MAIN-THREAD
    /// ONLY — it records a Vulkan transfer. `sRGB` picks the format: true for
    /// COLOR textures (base color, emissive), false (linear `UNORM`) for DATA
    /// textures (normal, metallic-roughness, occlusion).
    [[nodiscard]] uint32_t Upload(const DecodedImage& Image, bool sRGB, const char* DebugName);

    /// Convenience: `Decode` then `Upload` on the calling thread (single image).
    [[nodiscard]] uint32_t LoadImage(const void* Bytes, size_t Size, bool sRGB,
                                     const char* DebugName);

    /// Number of textures currently owned (debug / stats).
    [[nodiscard]] size_t Count() const noexcept { return m_textures.size(); }

private:
    rhi::Device* m_dev;
    std::vector<rhi::TextureHandle> m_textures;
};

} // namespace helio::resource
