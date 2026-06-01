find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

helio_add_module(RHI
    SOURCES
        Public/Device.cpp
        Public/RingUploadBuffer.cpp
        Vulkan/VulkanContext.cpp
        Vulkan/VulkanCommandList.cpp
        Vulkan/VulkanVMAImpl.cpp
        Vulkan/VulkanFormats.cpp
        Vulkan/VulkanBindless.cpp
        Vulkan/VulkanResources.cpp
        Vulkan/VulkanDeletionQueue.cpp
        Vulkan/VulkanUploader.cpp
        Vulkan/VulkanShaderCache.cpp
        Vulkan/VulkanPipeline.cpp
        Vulkan/VulkanAccelStructure.cpp
    PUBLIC_LINK
        Helio.Core
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        Vulkan::Headers
    PRIVATE_LINK
        SDL3::SDL3
)

# RHI's TUs include volk.h, which provides its own Vulkan prototypes. Stop the
# system <vulkan/vulkan.h> from being pulled in by SDL_vulkan.h.
target_compile_definitions(Helio.RHI PRIVATE
    VK_NO_PROTOTYPES
    SDL_ENABLE_OLDNAMES=0
)
