#include "TextureCache.h"

#include <Core/Logging/Log.h>

#include <RHI/Public/Device.h>

// Thread-local stb failure strings so parallel decodes don't race on the
// global error pointer.
#define STBI_THREAD_LOCAL thread_local
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO // we only ever decode from memory
#include <stb_image.h>

namespace helio::resource {

TextureCache::TextureCache(rhi::Device& Dev) : m_dev(&Dev) {}

TextureCache::~TextureCache() {
    for (const rhi::TextureHandle& T : m_textures) {
        if (T.IsValid()) {
            m_dev->DestroyTexture(T);
        }
    }
    m_textures.clear();
}

DecodedImage TextureCache::Decode(const void* Bytes, size_t Size, const char* DebugName) {
    DecodedImage Out;
    if (Bytes == nullptr || Size == 0) {
        return Out;
    }
    int W = 0, H = 0, SrcChannels = 0;
    // Force 4 channels (RGBA8) — the engine has no R/RG sampled-texture path in
    // the material shader, and forcing 4 keeps the upload tightly packed.
    stbi_uc* Pixels = stbi_load_from_memory(
        static_cast<const stbi_uc*>(Bytes), static_cast<int>(Size), &W, &H, &SrcChannels, 4);
    if (Pixels == nullptr || W <= 0 || H <= 0) {
        HELIO_LOG_WARN("Resource", "TextureCache: failed to decode image '{}' ({})",
                       DebugName ? DebugName : "?", stbi_failure_reason());
        if (Pixels) stbi_image_free(Pixels);
        return Out;
    }
    Out.W = W;
    Out.H = H;
    Out.Pixels.assign(Pixels, Pixels + static_cast<size_t>(W) * H * 4u);
    stbi_image_free(Pixels);
    return Out;
}

uint32_t TextureCache::Upload(const DecodedImage& Image, bool sRGB, const char* DebugName) {
    if (!Image.IsValid()) {
        return UINT32_MAX;
    }
    rhi::TextureHandle Tex = m_dev->CreateTexture({
        .Width = static_cast<uint32_t>(Image.W),
        .Height = static_cast<uint32_t>(Image.H),
        .Fmt = sRGB ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNORM,
        .Usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
        .DebugName = DebugName,
        .InitialData = Image.Pixels.data(),
        .InitialDataSize = Image.Pixels.size(),
        .GenerateMips = true,
    });
    if (!Tex.IsValid() || Tex.SampledSlot == UINT32_MAX) {
        return UINT32_MAX;
    }
    m_textures.push_back(Tex);
    return Tex.SampledSlot;
}

uint32_t TextureCache::LoadImage(const void* Bytes, size_t Size, bool sRGB, const char* DebugName) {
    return Upload(Decode(Bytes, Size, DebugName), sRGB, DebugName);
}

} // namespace helio::resource
