// VMA implementation translation unit.
//
// VMA is a header-only library; exactly one .cpp must define VMA_IMPLEMENTATION
// before including <vk_mem_alloc.h>. Isolated here so changes to other RHI
// sources don't recompile the 5000-line VMA implementation.
//
// We disable static Vulkan function resolution (volk replaces the prototype
// table) and enable VMA's dynamic loader, which uses the two function pointers
// (vkGetInstanceProcAddr / vkGetDeviceProcAddr) we hand it via VmaVulkanFunctions
// in VulkanContext.cpp.

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <volk.h>
#include <vk_mem_alloc.h>
