/// @file VulkanContext.h
/// @brief Vulkan 1.3 instance/device/swapchain owner.
///
/// Implements the public RHI Device interface. Owns:
/// - VkInstance + debug messenger
/// - VkSurfaceKHR (from SDL3)
/// - VkPhysicalDevice + VkDevice (Vulkan 1.3, bindless features, RT extensions)
/// - VkQueue (graphics + present, same family on every desktop GPU)
/// - VmaAllocator
/// - VkSwapchainKHR + image views
/// - Per-frame command pool/buffer + sync objects (frames-in-flight = 2)
#pragma once

#include <RHI/Public/Device.h>
#include <RHI/Public/CommandList.h>
#include "VulkanAccelStructure.h"
#include "VulkanBindless.h"
#include "VulkanCommandList.h"
#include "VulkanDeletionQueue.h"
#include "VulkanPipeline.h"
#include "VulkanResources.h"
#include "VulkanShaderCache.h"
#include "VulkanUploader.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#if HELIO_TRACY_ENABLED
    #include <tracy/TracyVulkan.hpp>
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace helio::rhi::vulkan {

inline constexpr uint32_t FramesInFlight = 2;

struct FrameResources {
    VkCommandPool CommandPool{VK_NULL_HANDLE};
    VkCommandBuffer CommandBuffer{VK_NULL_HANDLE};
    /// Signaled by vkAcquireNextImageKHR; waited on at queue submit.
    /// Per frame-in-flight: safe because we wait on the InFlight fence
    /// before re-using this semaphore.
    VkSemaphore ImageAcquired{VK_NULL_HANDLE};
    VkFence InFlight{VK_NULL_HANDLE};
};

class VulkanContext {
public:
    explicit VulkanContext(const DeviceConfig& Config);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    CommandList* BeginFrame();
    void ClearCurrentImage(float R, float G, float B, float A);
    void EndFrame();
    void WaitIdle();
    void Resize(int Width, int Height);

    // Phase 5 — resources.
    BufferHandle CreateBuffer(const BufferDesc& Desc);
    void DestroyBuffer(BufferHandle H);
    void UploadToBuffer(BufferHandle H, uint64_t Offset, const void* Data, uint64_t Size);
    TextureHandle CreateTexture(const TextureDesc& Desc);
    void DestroyTexture(TextureHandle H);
    Device::BindlessUsage GetBindlessUsage() const;

    // Capabilities.
    bool HasRayTracing() const noexcept { return m_hasRayTracing; }
    RayTracingProperties GetRayTracingProperties() const;
    /// Most-recently-retired frame's GPU duration in ms (2-frame lag).
    /// Returns 0.0 until the first complete cycle's data is available.
    double GetLastFrameGpuMs() const noexcept { return m_lastFrameGpuMs; }
    /// Slot currently being recorded (0..FramesInFlight-1).
    uint32_t GetCurrentFrameIndex() const noexcept { return m_frameIndex; }

    // Phase 6 — pipelines.
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& Desc);
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& Desc);
    void DestroyPipeline(PipelineHandle H);

    // Phase 8 — accel structures.
    BLASHandle BuildBLAS(const BLASDesc& Desc);
    void DestroyBLAS(BLASHandle H);
    TLASHandle BuildTLAS(const TLASDesc& Desc);
    void DestroyTLAS(TLASHandle H);
    void SetActiveTLAS(TLASHandle H);
    TLASHandle BuildVerificationTLAS();

    // Phase 7 — internals used by VulkanCommandList.cpp.
    const VulkanPipeline* LookupPipeline(PipelineHandle H) const;
    void BeginRenderingToSwapchainInternal(VulkanCommandListImpl* C, float R, float G, float B, float A);
    void BeginRenderingToTexturesInternal(VulkanCommandListImpl* C,
                                          const ColorAttachment* Colors, uint32_t NumColors,
                                          const DepthAttachment* Depth);
    void TransitionForSamplingInternal(VulkanCommandListImpl* C, TextureHandle H);
    void TransitionForStorageWriteInternal(VulkanCommandListImpl* C, TextureHandle H);
    void SetViewportFullInternal(VulkanCommandListImpl* C);
    void SetViewportToExtentInternal(VulkanCommandListImpl* C, uint32_t Width, uint32_t Height);
    void BindVertexBufferInternal(VulkanCommandListImpl* C, BufferHandle H, uint32_t Binding, uint64_t Offset);
    void BindIndexBufferInternal(VulkanCommandListImpl* C, BufferHandle H, VkIndexType Type, uint64_t Offset);
    void BlitImageInternal(VulkanCommandListImpl* C, TextureHandle Src, TextureHandle Dst, VkFilter Filter);
    void CopyImageInternal(VulkanCommandListImpl* C, TextureHandle Src, TextureHandle Dst);
    void BlitToSwapchainInternal(VulkanCommandListImpl* C, TextureHandle Src, VkFilter Filter);

    /// Accessors used by sibling RHI files in later phases.
    VkDevice GetDevice() const noexcept { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const noexcept { return m_physicalDevice; }
    VmaAllocator GetAllocator() const noexcept { return m_allocator; }
    VkQueue GetGraphicsQueue() const noexcept { return m_graphicsQueue; }
    uint32_t GetGraphicsQueueFamily() const noexcept { return m_graphicsQueueFamily; }
    VulkanBindless* GetBindless() noexcept { return m_bindless.get(); }
    VulkanResourcePool& GetResourcePool() noexcept { return m_pool; }

private:
    void CreateInstance();
    void CreateDebugMessenger();
    void CreateSurface(void* NativeWindow);
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();
    void CreateSwapchain(int Width, int Height);
    void DestroySwapchain();
    void CreateFrameResources();
    void DestroyFrameResources();
    void RecreateSwapchain();
    void LogPhysicalDeviceProperties();

    DeviceConfig m_config;

    VkInstance m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    uint32_t m_graphicsQueueFamily{UINT32_MAX};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_swapchainFormat{VK_FORMAT_UNDEFINED};
    VkColorSpaceKHR m_swapchainColorSpace{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkExtent2D m_swapchainExtent{};
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    /// One per swapchain image. Signaled at queue submit; waited on by present.
    /// Per-image because presentation holds the semaphore until the image is
    /// re-acquired (spec UID 03868).
    std::vector<VkSemaphore> m_imageRenderFinished;

    std::array<FrameResources, FramesInFlight> m_frames{};
    uint32_t m_frameIndex{0};
    uint32_t m_currentImageIndex{0};

    /// Actual RT availability after device pick — may differ from
    /// `m_config.EnableRayTracing` if the GPU couldn't satisfy the request.
    bool m_hasRayTracing{false};

    // Cached ray tracing properties (queried at device creation when supported).
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProps{};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_asProps{};

    // Phase 5 — bindless / resources.
    std::unique_ptr<VulkanBindless> m_bindless;
    std::unique_ptr<VulkanUploader> m_uploader;
    VulkanResourcePool m_pool;
    VulkanDeletionQueue m_deletion;

    // Phase 6 — pipelines / shaders.
    std::unique_ptr<VulkanShaderCache> m_shaderCache;
    std::unique_ptr<VulkanPipelineFactory> m_pipelineFactory;
    std::unordered_map<uint64_t, VulkanPipeline> m_pipelines;
    uint64_t m_nextPipelineId{1};

    // Phase 8 — accel structures.
    std::unique_ptr<VulkanAccelBuilder> m_accelBuilder;
    std::unordered_map<uint64_t, VulkanBLAS> m_blas;
    std::unordered_map<uint64_t, VulkanTLAS> m_tlas;
    uint64_t m_nextAccelId{1};
    TLASHandle m_activeTlas{};

    /// Last layout the current swapchain image was transitioned into during
    /// this frame. Used so EndFrame knows what to transition FROM.
    VkImageLayout m_currentImageLayout{VK_IMAGE_LAYOUT_UNDEFINED};

    // Phase 7 — CommandList per frame + Tracy GPU context.
    std::array<VulkanCommandListImpl, FramesInFlight> m_cmdImpls{};
    CommandList m_cmdList{};

    // Frame timing — one timestamp pair per frame-in-flight slot.
    // Slot i uses query indices [2*i, 2*i+1] (start, end). Written by
    // BeginFrame/EndFrame, read back ~2 frames later when the slot's fence
    // signals. `m_timestampHasPrior` tracks whether a slot has ever been
    // written so we don't read uninitialized queries on the first 2 frames.
    VkQueryPool                       m_timestampPool{VK_NULL_HANDLE};
    float                             m_timestampPeriodNs{0.0f};
    std::array<bool, FramesInFlight>  m_timestampHasPrior{};
    double                            m_lastFrameGpuMs{0.0};

#if HELIO_TRACY_ENABLED
    /// Tracy Vulkan profiling context. One per device — submits its own
    /// "calibration" commands and collects timestamps each frame.
    TracyVkCtx m_tracyCtx{nullptr};
    VkCommandPool m_tracySetupPool{VK_NULL_HANDLE};
    VkCommandBuffer m_tracySetupCmd{VK_NULL_HANDLE};
#endif
};

} // namespace helio::rhi::vulkan
