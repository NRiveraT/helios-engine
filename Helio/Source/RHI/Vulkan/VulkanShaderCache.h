/// @file VulkanShaderCache.h
/// @brief Loads compiled SPIR-V from disk into `VkShaderModule`s.
///
/// V1 caches modules per file path — same file requested twice returns the
/// same module. Phase 13 adds hot-reload via libslang + the file watcher.
#pragma once

#include <volk.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace helio::rhi::vulkan {

class VulkanShaderCache {
public:
    explicit VulkanShaderCache(VkDevice Device);
    void Shutdown();

    /// Load the SPIR-V at `RelPath` (relative to the binary directory) and
    /// return a `VkShaderModule`. Cached on subsequent calls. `.slang`
    /// extensions are silently rewritten to `.spv` so callers can keep the
    /// source path in their pipeline descs.
    [[nodiscard]] VkShaderModule Load(const std::string& RelPath);

private:
    [[nodiscard]] std::vector<uint32_t> ReadSpirv(const std::filesystem::path& Path);

    VkDevice m_device{VK_NULL_HANDLE};
    std::unordered_map<std::string, VkShaderModule> m_cache;
};

} // namespace helio::rhi::vulkan
