#include "VulkanShaderCache.h"
#include "VulkanCheck.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <fstream>
#include <vector>

namespace helio::rhi::vulkan {

VulkanShaderCache::VulkanShaderCache(VkDevice Device) : m_device(Device) {}

void VulkanShaderCache::Shutdown() {
    for (auto& [_, Mod] : m_cache) {
        if (Mod) vkDestroyShaderModule(m_device, Mod, nullptr);
    }
    m_cache.clear();
}

VkShaderModule VulkanShaderCache::Load(const std::string& RelPath) {
    // Rewrite .slang -> .spv so callers can use the source path verbatim.
    std::string Key = RelPath;
    if (Key.size() >= 6 && Key.compare(Key.size() - 6, 6, ".slang") == 0) {
        Key.replace(Key.size() - 6, 6, ".spv");
    }

    if (auto It = m_cache.find(Key); It != m_cache.end()) {
        return It->second;
    }

    std::filesystem::path Path{Key};
    auto Bytes = ReadSpirv(Path);
    if (Bytes.empty()) {
        HELIO_LOG_ERROR("RHI", "ShaderCache: failed to read '{}'", Key);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo CI{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    CI.codeSize = Bytes.size() * sizeof(uint32_t);
    CI.pCode = Bytes.data();

    VkShaderModule Mod = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(m_device, &CI, nullptr, &Mod));
    m_cache.emplace(Key, Mod);
    HELIO_LOG_INFO("RHI", "ShaderCache loaded '{}' ({} bytes SPIR-V)", Key, CI.codeSize);
    return Mod;
}

std::vector<uint32_t> VulkanShaderCache::ReadSpirv(const std::filesystem::path& Path) {
    std::ifstream In(Path, std::ios::binary | std::ios::ate);
    if (!In) return {};
    auto Size = static_cast<std::streamsize>(In.tellg());
    if (Size <= 0 || (Size % 4) != 0) return {};

    std::vector<uint32_t> Words(static_cast<size_t>(Size / 4));
    In.seekg(0);
    In.read(reinterpret_cast<char*>(Words.data()), Size);
    return Words;
}

} // namespace helio::rhi::vulkan
