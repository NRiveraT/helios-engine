#include "VulkanContext.h"
#include "VulkanCheck.h"
#include "VulkanFormats.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace helio::rhi::vulkan {

// =============================================================================
// VkResult string table
// =============================================================================
const char* VkResultToString(VkResult R) noexcept {
    switch (R) {
        case VK_SUCCESS:                        return "VK_SUCCESS";
        case VK_NOT_READY:                      return "VK_NOT_READY";
        case VK_TIMEOUT:                        return "VK_TIMEOUT";
        case VK_EVENT_SET:                      return "VK_EVENT_SET";
        case VK_EVENT_RESET:                    return "VK_EVENT_RESET";
        case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
        default:                                return "<unknown VkResult>";
    }
}

namespace {

// -----------------------------------------------------------------------------
// Debug messenger callback — routes Vulkan validation/info to spdlog.
// -----------------------------------------------------------------------------
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
    VkDebugUtilsMessageTypeFlagsEXT /*Type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* Data,
    void* /*UserData*/) {

    const char* Msg = Data && Data->pMessage ? Data->pMessage : "<null>";
    switch (Severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            HELIO_LOG_ERROR("Vulkan", "{}", Msg);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            HELIO_LOG_WARN("Vulkan", "{}", Msg);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            HELIO_LOG_INFO("Vulkan", "{}", Msg);
            break;
        default:
            HELIO_LOG_DEBUG("Vulkan", "{}", Msg);
            break;
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT CI{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    CI.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    CI.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    CI.pfnUserCallback = DebugCallback;
    return CI;
}

bool LayerSupported(const char* Name) {
    uint32_t Count = 0;
    vkEnumerateInstanceLayerProperties(&Count, nullptr);
    std::vector<VkLayerProperties> Layers(Count);
    vkEnumerateInstanceLayerProperties(&Count, Layers.data());
    for (const auto& L : Layers) {
        if (std::strcmp(L.layerName, Name) == 0) return true;
    }
    return false;
}

bool DeviceSupportsExtension(VkPhysicalDevice Dev, const char* Name) {
    uint32_t Count = 0;
    vkEnumerateDeviceExtensionProperties(Dev, nullptr, &Count, nullptr);
    std::vector<VkExtensionProperties> Exts(Count);
    vkEnumerateDeviceExtensionProperties(Dev, nullptr, &Count, Exts.data());
    for (const auto& E : Exts) {
        if (std::strcmp(E.extensionName, Name) == 0) return true;
    }
    return false;
}

} // namespace

// =============================================================================
// Lifecycle
// =============================================================================
VulkanContext::VulkanContext(const DeviceConfig& Config)
    : m_config(Config) {

    HELIO_LOG_INFO("RHI", "VulkanContext starting up. Validation={} RT={}",
                   m_config.EnableValidation, m_config.EnableRayTracing);

    VK_CHECK(volkInitialize());

    CreateInstance();
    volkLoadInstance(m_instance);

    if (m_config.EnableValidation) CreateDebugMessenger();

    CreateSurface(m_config.NativeWindow);
    PickPhysicalDevice();
    LogPhysicalDeviceProperties();
    CreateLogicalDevice();
    volkLoadDevice(m_device);

    CreateAllocator();
    CreateSwapchain(m_config.InitialWidth, m_config.InitialHeight);
    CreateFrameResources();

    m_bindless = std::make_unique<VulkanBindless>(m_device);
    m_uploader = std::make_unique<VulkanUploader>(m_device, m_allocator, m_graphicsQueue, m_graphicsQueueFamily);
    m_shaderCache = std::make_unique<VulkanShaderCache>(m_device);
    m_pipelineFactory = std::make_unique<VulkanPipelineFactory>(m_device, *m_bindless, *m_shaderCache);
    if (m_hasRayTracing) {
        m_accelBuilder = std::make_unique<VulkanAccelBuilder>(*this);
    }

    // Wire each per-frame CmdList impl to its command buffer + this context.
    for (uint32_t I = 0; I < FramesInFlight; ++I) {
        m_cmdImpls[I].Ctx = this;
        m_cmdImpls[I].Cmd = m_frames[I].CommandBuffer;
    }

#if HELIO_TRACY_ENABLED
    // Tracy Vulkan profiling context. Needs a one-shot command buffer for the
    // calibration cycle. The ctx persists for the device lifetime.
    VkCommandPoolCreateInfo PCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    PCI.queueFamilyIndex = m_graphicsQueueFamily;
    PCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &PCI, nullptr, &m_tracySetupPool));
    VkCommandBufferAllocateInfo BAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    BAI.commandPool = m_tracySetupPool;
    BAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    BAI.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &BAI, &m_tracySetupCmd));
    m_tracyCtx = TracyVkContext(m_physicalDevice, m_device, m_graphicsQueue, m_tracySetupCmd);
    for (auto& C : m_cmdImpls) C.Tracy = m_tracyCtx;
    HELIO_LOG_INFO("RHI", "Tracy GPU profiling context created.");
#endif

    HELIO_LOG_INFO("RHI", "VulkanContext ready. Swapchain {}x{}, {} images, {} frames in flight.",
                   m_swapchainExtent.width, m_swapchainExtent.height,
                   m_swapchainImages.size(), FramesInFlight);
}

VulkanContext::~VulkanContext() {
    if (m_device) vkDeviceWaitIdle(m_device);

    m_deletion.FlushAll();

    // Drain any user-created resources that were never explicitly destroyed.
    // Without this, persistent textures/buffers leak through vmaDestroyAllocator
    // below and VMA hits an "allocations were not freed" assertion.
    auto& BufferMap = m_pool.GetBufferMapForShutdown();
    for (auto& [Id, B] : BufferMap) {
        if (B.Buffer) vmaDestroyBuffer(m_allocator, B.Buffer, B.Allocation);
        if (B.BindlessSlot != UINT32_MAX) m_bindless->FreeStorageBuffer(B.BindlessSlot);
    }
    BufferMap.clear();
    auto& TextureMap = m_pool.GetTextureMapForShutdown();
    for (auto& [Id, T] : TextureMap) {
        if (T.View)  vkDestroyImageView(m_device, T.View, nullptr);
        if (T.Image) vmaDestroyImage(m_allocator, T.Image, T.Allocation);
        if (T.SampledSlot != UINT32_MAX) m_bindless->FreeSampledImage(T.SampledSlot);
        if (T.StorageSlot != UINT32_MAX) m_bindless->FreeStorageImage(T.StorageSlot);
    }
    TextureMap.clear();

#if HELIO_TRACY_ENABLED
    if (m_tracyCtx) {
        TracyVkDestroy(m_tracyCtx);
        m_tracyCtx = nullptr;
    }
    if (m_tracySetupPool) {
        vkDestroyCommandPool(m_device, m_tracySetupPool, nullptr);
        m_tracySetupPool = VK_NULL_HANDLE;
        m_tracySetupCmd = VK_NULL_HANDLE;
    }
#endif

    for (auto& [_, P] : m_pipelines) {
        if (P.Pipeline) vkDestroyPipeline(m_device, P.Pipeline, nullptr);
    }
    m_pipelines.clear();

    // Phase 8 — accel structures
    for (auto& [_, T] : m_tlas) {
        if (T.Accel) vkDestroyAccelerationStructureKHR(m_device, T.Accel, nullptr);
        if (T.Storage) vmaDestroyBuffer(m_allocator, T.Storage, T.StorageAlloc);
        if (T.InstanceBuf) vmaDestroyBuffer(m_allocator, T.InstanceBuf, T.InstanceAlloc);
    }
    m_tlas.clear();
    for (auto& [_, B] : m_blas) {
        if (B.Accel) vkDestroyAccelerationStructureKHR(m_device, B.Accel, nullptr);
        if (B.Storage) vmaDestroyBuffer(m_allocator, B.Storage, B.StorageAlloc);
    }
    m_blas.clear();
    if (m_accelBuilder) {
        m_accelBuilder->Shutdown();
        m_accelBuilder.reset();
    }

    if (m_pipelineFactory) {
        m_pipelineFactory->Shutdown();
        m_pipelineFactory.reset();
    }
    if (m_shaderCache) {
        m_shaderCache->Shutdown();
        m_shaderCache.reset();
    }
    if (m_uploader) {
        m_uploader->Shutdown();
        m_uploader.reset();
    }
    if (m_bindless) {
        m_bindless->Shutdown(m_device);
        m_bindless.reset();
    }

    DestroyFrameResources();
    DestroySwapchain();

    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_surface) {
        SDL_Vulkan_DestroySurface(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_debugMessenger) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    HELIO_LOG_INFO("RHI", "VulkanContext destroyed.");
}

// =============================================================================
// Instance + Debug messenger
// =============================================================================
void VulkanContext::CreateInstance() {
    VkApplicationInfo App{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    App.pApplicationName = "Helio Game";
    App.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    App.pEngineName = "Helio";
    App.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    App.apiVersion = VK_API_VERSION_1_3;

    // SDL3 tells us which extensions are required for surface support.
    uint32_t SDLExtCount = 0;
    const char* const* SDLExts = SDL_Vulkan_GetInstanceExtensions(&SDLExtCount);
    HELIO_CHECK(SDLExts);

    std::vector<const char*> Extensions(SDLExts, SDLExts + SDLExtCount);
    if (m_config.EnableValidation) {
        Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> Layers;
    if (m_config.EnableValidation) {
        if (LayerSupported("VK_LAYER_KHRONOS_validation")) {
            Layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            HELIO_LOG_WARN("RHI", "VK_LAYER_KHRONOS_validation requested but not available.");
        }
    }

    VkInstanceCreateInfo CI{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    CI.pApplicationInfo = &App;
    CI.enabledExtensionCount = static_cast<uint32_t>(Extensions.size());
    CI.ppEnabledExtensionNames = Extensions.data();
    CI.enabledLayerCount = static_cast<uint32_t>(Layers.size());
    CI.ppEnabledLayerNames = Layers.data();

    // Attach debug messenger create info so we get callbacks during create/destroy too.
    auto DbgCI = MakeDebugCreateInfo();
    if (m_config.EnableValidation && !Layers.empty()) {
        CI.pNext = &DbgCI;
    }

    VK_CHECK(vkCreateInstance(&CI, nullptr, &m_instance));
    HELIO_LOG_INFO("RHI", "Vulkan instance created ({} extensions, {} layers).", Extensions.size(), Layers.size());
}

void VulkanContext::CreateDebugMessenger() {
    auto CI = MakeDebugCreateInfo();
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &CI, nullptr, &m_debugMessenger));
}

// =============================================================================
// Surface
// =============================================================================
void VulkanContext::CreateSurface(void* NativeWindow) {
    HELIO_CHECK(NativeWindow);
    SDL_Window* Window = static_cast<SDL_Window*>(NativeWindow);
    if (!SDL_Vulkan_CreateSurface(Window, m_instance, nullptr, &m_surface)) {
        HELIO_LOG_CRITICAL("RHI", "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        HELIO_CHECK(false);
    }
}

// =============================================================================
// Physical device selection
// =============================================================================
namespace {

struct DeviceScore {
    VkPhysicalDevice Device{VK_NULL_HANDLE};
    int Score{-1};
    uint32_t QueueFamily{UINT32_MAX};
};

uint32_t FindGraphicsPresentQueueFamily(VkPhysicalDevice Dev, VkSurfaceKHR Surface) {
    uint32_t Count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(Dev, &Count, nullptr);
    std::vector<VkQueueFamilyProperties> Families(Count);
    vkGetPhysicalDeviceQueueFamilyProperties(Dev, &Count, Families.data());
    for (uint32_t I = 0; I < Count; ++I) {
        if (!(Families[I].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 PresentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(Dev, I, Surface, &PresentSupport);
        if (PresentSupport) return I;
    }
    return UINT32_MAX;
}

} // namespace

namespace {

bool DeviceSupportsAllRT(VkPhysicalDevice Dev) {
    return DeviceSupportsExtension(Dev, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        && DeviceSupportsExtension(Dev, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
        && DeviceSupportsExtension(Dev, VK_KHR_RAY_QUERY_EXTENSION_NAME)
        && DeviceSupportsExtension(Dev, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
}

} // namespace

void VulkanContext::PickPhysicalDevice() {
    uint32_t Count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &Count, nullptr));
    HELIO_CHECK(Count > 0);
    std::vector<VkPhysicalDevice> Devices(Count);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &Count, Devices.data()));

    // Two-pass: if RT was requested, prefer RT-capable devices but accept a
    // non-RT device as fallback. Within the same RT tier, higher score wins
    // (discrete > integrated).
    DeviceScore BestRT{};       // qualifies for RT
    DeviceScore BestNonRT{};    // works but no RT

    for (auto Dev : Devices) {
        VkPhysicalDeviceProperties Props{};
        vkGetPhysicalDeviceProperties(Dev, &Props);

        if (Props.apiVersion < VK_API_VERSION_1_3) continue;
        if (!DeviceSupportsExtension(Dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        uint32_t Family = FindGraphicsPresentQueueFamily(Dev, m_surface);
        if (Family == UINT32_MAX) continue;

        int Score = 0;
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) Score += 1000;
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) Score += 100;

        bool RTCapable = DeviceSupportsAllRT(Dev);
        HELIO_LOG_INFO("RHI", "Candidate GPU: {} (score {}, RT={})",
                       Props.deviceName, Score, RTCapable);

        DeviceScore& Bucket = RTCapable ? BestRT : BestNonRT;
        if (Score > Bucket.Score) {
            Bucket = {Dev, Score, Family};
        }
    }

    DeviceScore Picked{};
    if (m_config.EnableRayTracing && BestRT.Device != VK_NULL_HANDLE) {
        Picked = BestRT;
        m_hasRayTracing = true;
    } else {
        Picked = BestNonRT.Device != VK_NULL_HANDLE ? BestNonRT : BestRT;
        m_hasRayTracing = false;
        if (m_config.EnableRayTracing && Picked.Device != VK_NULL_HANDLE) {
            HELIO_LOG_WARN("RHI", "Ray tracing was requested but no Vulkan 1.3 GPU on this system "
                                  "exposes the RT extension set. Falling back to non-RT device.");
        }
    }

    if (Picked.Device == VK_NULL_HANDLE) {
        HELIO_LOG_CRITICAL("RHI", "No Vulkan 1.3 device found (RT requested={}).", m_config.EnableRayTracing);
        HELIO_CHECK(false);
    }

    m_physicalDevice = Picked.Device;
    m_graphicsQueueFamily = Picked.QueueFamily;
}

void VulkanContext::LogPhysicalDeviceProperties() {
    VkPhysicalDeviceProperties Props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &Props);
    HELIO_LOG_INFO("RHI", "Selected GPU: {} (driver {:#x}, API {}.{}.{})",
                   Props.deviceName, Props.driverVersion,
                   VK_API_VERSION_MAJOR(Props.apiVersion),
                   VK_API_VERSION_MINOR(Props.apiVersion),
                   VK_API_VERSION_PATCH(Props.apiVersion));

    // Cache timestamp period (ns per tick) for whole-frame GPU timing.
    // Zero means timestamps aren't supported on the graphics queue — every
    // desktop GPU supports them but be defensive.
    m_timestampPeriodNs = Props.limits.timestampPeriod;
    if (m_timestampPeriodNs <= 0.0f) {
        HELIO_LOG_WARN("RHI", "Timestamps not supported on this device; "
                              "Device::LastFrameGpuMs() will return 0.");
    }

    if (m_hasRayTracing) {
        m_rtProps = VkPhysicalDeviceRayTracingPipelinePropertiesKHR{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
        m_asProps = VkPhysicalDeviceAccelerationStructurePropertiesKHR{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        m_rtProps.pNext = &m_asProps;

        VkPhysicalDeviceProperties2 P2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        P2.pNext = &m_rtProps;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &P2);

        HELIO_LOG_INFO("RHI", "RT: maxRayRecursionDepth={} shaderGroupHandleSize={} maxGeometry={}",
                       m_rtProps.maxRayRecursionDepth, m_rtProps.shaderGroupHandleSize,
                       m_asProps.maxGeometryCount);
    } else {
        HELIO_LOG_INFO("RHI", "RT not available on selected device.");
    }
}

// =============================================================================
// Logical device + feature chain
// =============================================================================
void VulkanContext::CreateLogicalDevice() {
    float Priority = 1.0f;
    VkDeviceQueueCreateInfo QCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    QCI.queueFamilyIndex = m_graphicsQueueFamily;
    QCI.queueCount = 1;
    QCI.pQueuePriorities = &Priority;

    // Feature chain (pNext-linked).
    VkPhysicalDeviceRayQueryFeaturesKHR RQF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR RTF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR ASF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceVulkan13Features V13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features V12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features V11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 F2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

    V11.shaderDrawParameters = VK_TRUE;

    V12.descriptorIndexing = VK_TRUE;
    V12.descriptorBindingPartiallyBound = VK_TRUE;
    V12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    V12.runtimeDescriptorArray = VK_TRUE;
    V12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    V12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    V12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    V12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    V12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    V12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    V12.bufferDeviceAddress = VK_TRUE;
    V12.timelineSemaphore = VK_TRUE;
    V12.scalarBlockLayout = VK_TRUE;

    V13.dynamicRendering = VK_TRUE;
    V13.synchronization2 = VK_TRUE;
    V13.maintenance4 = VK_TRUE;

    ASF.accelerationStructure = VK_TRUE;
    RTF.rayTracingPipeline = VK_TRUE;
    RQF.rayQuery = VK_TRUE;

    F2.pNext = &V13;
    V13.pNext = &V12;
    V12.pNext = &V11;
    if (m_hasRayTracing) {
        V11.pNext = &ASF;
        ASF.pNext = &RTF;
        RTF.pNext = &RQF;
    }

    // Sanity: verify the GPU supports what we asked for.
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &F2);
    HELIO_CHECK(V13.dynamicRendering && V13.synchronization2);
    HELIO_CHECK(V12.descriptorIndexing && V12.bufferDeviceAddress && V12.timelineSemaphore);
    HELIO_CHECK(V11.shaderDrawParameters);
    if (m_hasRayTracing) {
        HELIO_CHECK(ASF.accelerationStructure && RTF.rayTracingPipeline && RQF.rayQuery);
    }

    std::vector<const char*> Extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (m_hasRayTracing) {
        Extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        Extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        Extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        Extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }

    VkDeviceCreateInfo CI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    CI.pNext = &F2;
    CI.queueCreateInfoCount = 1;
    CI.pQueueCreateInfos = &QCI;
    CI.enabledExtensionCount = static_cast<uint32_t>(Extensions.size());
    CI.ppEnabledExtensionNames = Extensions.data();

    VK_CHECK(vkCreateDevice(m_physicalDevice, &CI, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    HELIO_LOG_INFO("RHI", "Logical device + queue (family {}) created.", m_graphicsQueueFamily);
}

// =============================================================================
// VMA
// =============================================================================
void VulkanContext::CreateAllocator() {
    VmaVulkanFunctions Fns{};
    Fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    Fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo CI{};
    CI.physicalDevice = m_physicalDevice;
    CI.device = m_device;
    CI.instance = m_instance;
    CI.vulkanApiVersion = VK_API_VERSION_1_3;
    CI.pVulkanFunctions = &Fns;
    CI.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&CI, &m_allocator));
    HELIO_LOG_INFO("RHI", "VMA allocator created.");
}

// =============================================================================
// Swapchain
// =============================================================================
void VulkanContext::CreateSwapchain(int Width, int Height) {
    VkSurfaceCapabilitiesKHR Caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &Caps));

    uint32_t FmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &FmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> Formats(FmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &FmtCount, Formats.data());

    // Prefer BGRA8 SRGB; fall back to first.
    VkSurfaceFormatKHR Picked = Formats[0];
    for (const auto& F : Formats) {
        if (F.format == VK_FORMAT_B8G8R8A8_SRGB && F.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            Picked = F;
            break;
        }
    }
    m_swapchainFormat = Picked.format;
    m_swapchainColorSpace = Picked.colorSpace;

    // Prefer MAILBOX, fall back to FIFO (always supported).
    uint32_t PMCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &PMCount, nullptr);
    std::vector<VkPresentModeKHR> PresentModes(PMCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &PMCount, PresentModes.data());
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto M : PresentModes) {
        if (M == VK_PRESENT_MODE_MAILBOX_KHR) { PresentMode = M; break; }
    }

    // Resolve extent.
    if (Caps.currentExtent.width != UINT32_MAX) {
        m_swapchainExtent = Caps.currentExtent;
    } else {
        m_swapchainExtent.width = std::clamp(static_cast<uint32_t>(Width),
                                             Caps.minImageExtent.width, Caps.maxImageExtent.width);
        m_swapchainExtent.height = std::clamp(static_cast<uint32_t>(Height),
                                              Caps.minImageExtent.height, Caps.maxImageExtent.height);
    }

    uint32_t ImageCount = Caps.minImageCount + 1;
    if (Caps.maxImageCount > 0 && ImageCount > Caps.maxImageCount) {
        ImageCount = Caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR CI{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    CI.surface = m_surface;
    CI.minImageCount = ImageCount;
    CI.imageFormat = m_swapchainFormat;
    CI.imageColorSpace = m_swapchainColorSpace;
    CI.imageExtent = m_swapchainExtent;
    CI.imageArrayLayers = 1;
    CI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    CI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CI.preTransform = Caps.currentTransform;
    CI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    CI.presentMode = PresentMode;
    CI.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_device, &CI, nullptr, &m_swapchain));

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &ImageCount, nullptr);
    m_swapchainImages.resize(ImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &ImageCount, m_swapchainImages.data());

    m_swapchainImageViews.resize(ImageCount);
    m_imageRenderFinished.resize(ImageCount);
    VkSemaphoreCreateInfo SCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t I = 0; I < ImageCount; ++I) {
        VkImageViewCreateInfo VCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        VCI.image = m_swapchainImages[I];
        VCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        VCI.format = m_swapchainFormat;
        VCI.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        VCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_device, &VCI, nullptr, &m_swapchainImageViews[I]));

        VK_CHECK(vkCreateSemaphore(m_device, &SCI, nullptr, &m_imageRenderFinished[I]));
    }

    HELIO_LOG_INFO("RHI", "Swapchain created: {}x{} {} images mode={}",
                   m_swapchainExtent.width, m_swapchainExtent.height,
                   ImageCount, static_cast<int>(PresentMode));
}

void VulkanContext::DestroySwapchain() {
    for (auto S : m_imageRenderFinished) {
        if (S) vkDestroySemaphore(m_device, S, nullptr);
    }
    m_imageRenderFinished.clear();
    for (auto V : m_swapchainImageViews) {
        if (V) vkDestroyImageView(m_device, V, nullptr);
    }
    m_swapchainImageViews.clear();
    m_swapchainImages.clear();
    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

// =============================================================================
// Per-frame command pool / buffer / sync
// =============================================================================
void VulkanContext::CreateFrameResources() {
    VkCommandPoolCreateInfo PCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    PCI.queueFamilyIndex = m_graphicsQueueFamily;
    PCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkSemaphoreCreateInfo SCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo FCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    FCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& F : m_frames) {
        VK_CHECK(vkCreateCommandPool(m_device, &PCI, nullptr, &F.CommandPool));

        VkCommandBufferAllocateInfo BAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        BAI.commandPool = F.CommandPool;
        BAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        BAI.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &BAI, &F.CommandBuffer));

        VK_CHECK(vkCreateSemaphore(m_device, &SCI, nullptr, &F.ImageAcquired));
        VK_CHECK(vkCreateFence(m_device, &FCI, nullptr, &F.InFlight));
    }

    // Timestamp pool: one (start, end) pair per frame-in-flight slot.
    if (m_timestampPeriodNs > 0.0f) {
        VkQueryPoolCreateInfo QPCI{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        QPCI.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        QPCI.queryCount = FramesInFlight * 2;
        VK_CHECK(vkCreateQueryPool(m_device, &QPCI, nullptr, &m_timestampPool));
    }
    m_timestampHasPrior.fill(false);
    m_lastFrameGpuMs = 0.0;
}

void VulkanContext::DestroyFrameResources() {
    if (m_timestampPool) {
        vkDestroyQueryPool(m_device, m_timestampPool, nullptr);
        m_timestampPool = VK_NULL_HANDLE;
    }
    for (auto& F : m_frames) {
        if (F.InFlight) vkDestroyFence(m_device, F.InFlight, nullptr);
        if (F.ImageAcquired) vkDestroySemaphore(m_device, F.ImageAcquired, nullptr);
        if (F.CommandPool) vkDestroyCommandPool(m_device, F.CommandPool, nullptr);
        F = {};
    }
}

// =============================================================================
// Frame loop
// =============================================================================
CommandList* VulkanContext::BeginFrame() {
    auto& Frame = m_frames[m_frameIndex];

    vkWaitForFences(m_device, 1, &Frame.InFlight, VK_TRUE, UINT64_MAX);

    // The frame slot's prior GPU work has completed — safe to run deferred
    // destructors that were queued the last time we were on this slot.
    m_deletion.OnFrameRetired(m_frameIndex);

    VkResult Acquire = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                             Frame.ImageAcquired, VK_NULL_HANDLE,
                                             &m_currentImageIndex);
    if (Acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return nullptr;
    }
    if (Acquire != VK_SUCCESS && Acquire != VK_SUBOPTIMAL_KHR) {
        VK_CHECK(Acquire);
    }

    vkResetFences(m_device, 1, &Frame.InFlight);
    vkResetCommandBuffer(Frame.CommandBuffer, 0);

    // Read back this slot's timestamps from its previous occupant (now retired
    // because we just waited on InFlight). The first FramesInFlight times this
    // runs the slot has never been written, so skip.
    if (m_timestampPool && m_timestampHasPrior[m_frameIndex]) {
        const uint32_t QStart = m_frameIndex * 2;
        uint64_t Tstamps[2]{};
        VkResult R = vkGetQueryPoolResults(m_device, m_timestampPool, QStart, 2,
                                           sizeof(Tstamps), Tstamps,
                                           sizeof(uint64_t),
                                           VK_QUERY_RESULT_64_BIT);
        if (R == VK_SUCCESS) {
            const uint64_t Delta = Tstamps[1] - Tstamps[0];
            m_lastFrameGpuMs = double(Delta) * double(m_timestampPeriodNs) * 1e-6;
        }
    }

    VkCommandBufferBeginInfo BI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    BI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(Frame.CommandBuffer, &BI));

    // Reset this slot's 2 query results (it'll be re-written this frame) and
    // record the frame-start timestamp at top-of-pipe.
    if (m_timestampPool) {
        const uint32_t QStart = m_frameIndex * 2;
        vkCmdResetQueryPool(Frame.CommandBuffer, m_timestampPool, QStart, 2);
        vkCmdWriteTimestamp2(Frame.CommandBuffer,
                             VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                             m_timestampPool, QStart);
    }

    m_currentImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Hand the user a CommandList pointing at the current frame's recording state.
    ResetCommandList(m_cmdList, m_cmdImpls[m_frameIndex]);
    return &m_cmdList;
}

void VulkanContext::ClearCurrentImage(float R, float G, float B, float A) {
    auto& Frame = m_frames[m_frameIndex];
    VkCommandBuffer Cmd = Frame.CommandBuffer;
    VkImage Img = m_swapchainImages[m_currentImageIndex];

    VkImageMemoryBarrier2 ToTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ToTransfer.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    ToTransfer.srcAccessMask = 0;
    ToTransfer.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    ToTransfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    ToTransfer.oldLayout = m_currentImageLayout;
    ToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToTransfer.image = Img;
    ToTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo Dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    Dep.imageMemoryBarrierCount = 1;
    Dep.pImageMemoryBarriers = &ToTransfer;
    vkCmdPipelineBarrier2(Cmd, &Dep);

    VkClearColorValue Color{};
    Color.float32[0] = R; Color.float32[1] = G; Color.float32[2] = B; Color.float32[3] = A;
    VkImageSubresourceRange Range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(Cmd, Img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);
    m_currentImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

void VulkanContext::EndFrame() {
    auto& CmdImpl = m_cmdImpls[m_frameIndex];
    if (CmdImpl.InRendering) {
        vkCmdEndRendering(CmdImpl.Cmd);
        CmdImpl.InRendering = false;
    }
    auto& Frame = m_frames[m_frameIndex];
    VkCommandBuffer Cmd = Frame.CommandBuffer;
    VkImage Img = m_swapchainImages[m_currentImageIndex];

#if HELIO_TRACY_ENABLED
    // Collect GPU timestamps for this frame's command buffer just before End.
    if (m_tracyCtx) TracyVkCollect(m_tracyCtx, Cmd);
#endif

    VkImageMemoryBarrier2 ToPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ToPresent.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ToPresent.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    ToPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    ToPresent.dstAccessMask = 0;
    ToPresent.oldLayout = m_currentImageLayout;
    ToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    ToPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToPresent.image = Img;
    ToPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkDependencyInfo Dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    Dep.imageMemoryBarrierCount = 1;
    Dep.pImageMemoryBarriers = &ToPresent;
    vkCmdPipelineBarrier2(Cmd, &Dep);

    // End-of-frame timestamp at bottom-of-pipe, just before End. The CPU
    // reads this back on next BeginFrame's wait-for-fence cycle (~2 frames).
    if (m_timestampPool) {
        const uint32_t QEnd = m_frameIndex * 2 + 1;
        vkCmdWriteTimestamp2(Cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                             m_timestampPool, QEnd);
        m_timestampHasPrior[m_frameIndex] = true;
    }

    VK_CHECK(vkEndCommandBuffer(Cmd));

    VkSemaphoreSubmitInfo WaitSem{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    WaitSem.semaphore = Frame.ImageAcquired;
    WaitSem.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphore RenderFinished = m_imageRenderFinished[m_currentImageIndex];
    VkSemaphoreSubmitInfo SignalSem{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    SignalSem.semaphore = RenderFinished;
    SignalSem.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo CmdSI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    CmdSI.commandBuffer = Cmd;

    VkSubmitInfo2 Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    Submit.waitSemaphoreInfoCount = 1;
    Submit.pWaitSemaphoreInfos = &WaitSem;
    Submit.signalSemaphoreInfoCount = 1;
    Submit.pSignalSemaphoreInfos = &SignalSem;
    Submit.commandBufferInfoCount = 1;
    Submit.pCommandBufferInfos = &CmdSI;

    VK_CHECK(vkQueueSubmit2(m_graphicsQueue, 1, &Submit, Frame.InFlight));

    VkPresentInfoKHR PI{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    PI.waitSemaphoreCount = 1;
    PI.pWaitSemaphores = &RenderFinished;
    PI.swapchainCount = 1;
    PI.pSwapchains = &m_swapchain;
    PI.pImageIndices = &m_currentImageIndex;

    VkResult Pres = vkQueuePresentKHR(m_graphicsQueue, &PI);
    if (Pres == VK_ERROR_OUT_OF_DATE_KHR || Pres == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain();
    } else if (Pres != VK_SUCCESS) {
        VK_CHECK(Pres);
    }

    m_frameIndex = (m_frameIndex + 1) % FramesInFlight;
}

void VulkanContext::WaitIdle() {
    if (m_device) vkDeviceWaitIdle(m_device);
}

void VulkanContext::Resize(int Width, int Height) {
    m_config.InitialWidth = Width;
    m_config.InitialHeight = Height;
    RecreateSwapchain();
}

void VulkanContext::RecreateSwapchain() {
    vkDeviceWaitIdle(m_device);
    DestroySwapchain();
    CreateSwapchain(m_config.InitialWidth, m_config.InitialHeight);
}

// =============================================================================
// Phase 5 — buffers + textures + bindless slot wiring
// =============================================================================
BufferHandle VulkanContext::CreateBuffer(const BufferDesc& Desc) {
    HELIO_CHECK(Desc.Size > 0);

    VulkanBuffer B{};
    B.Size = Desc.Size;
    B.Usage = Desc.Usage;
    B.Memory = Desc.Memory;

    BufferUsage VkU = Desc.Usage;
    // Anything we want to upload into needs TransferDst implicitly.
    if (Desc.InitialData) VkU = VkU | BufferUsage::TransferDst;
    // Storage buffers in our bindless model always allow shader device address
    // so they can also participate in ray queries / pointer chasing.
    if (HasFlag(VkU, BufferUsage::Storage)) VkU = VkU | BufferUsage::ShaderDeviceAddress;

    VkBufferCreateInfo BCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    BCI.size = Desc.Size;
    BCI.usage = ToVk(VkU);
    BCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ACI{};
    ACI.usage = MemoryHint(Desc.Memory);
    ACI.flags = MemoryFlags(Desc.Memory);

    VmaAllocationInfo Info{};
    VK_CHECK(vmaCreateBuffer(m_allocator, &BCI, &ACI, &B.Buffer, &B.Allocation, &Info));
    B.MappedPtr = Info.pMappedData;

    if (Desc.DebugName) {
        VkDebugUtilsObjectNameInfoEXT N{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        N.objectType = VK_OBJECT_TYPE_BUFFER;
        N.objectHandle = reinterpret_cast<uint64_t>(B.Buffer);
        N.pObjectName = Desc.DebugName;
        if (vkSetDebugUtilsObjectNameEXT) vkSetDebugUtilsObjectNameEXT(m_device, &N);
    }

    if (HasFlag(Desc.Usage, BufferUsage::Storage)) {
        B.BindlessSlot = m_bindless->AllocateStorageBuffer();
        HELIO_CHECK(B.BindlessSlot != UINT32_MAX);
        m_bindless->WriteStorageBuffer(m_device, B.BindlessSlot, B.Buffer, 0, Desc.Size);
    }

    if (Desc.InitialData) {
        HELIO_CHECK(Desc.InitialDataSize <= Desc.Size);
        if (Desc.Memory == MemoryUsage::DeviceLocal) {
            m_uploader->UploadToBuffer(B.Buffer, 0, Desc.InitialData, Desc.InitialDataSize);
        } else {
            HELIO_CHECK(B.MappedPtr);
            std::memcpy(B.MappedPtr, Desc.InitialData, Desc.InitialDataSize);
        }
    }

    BufferHandle H{};
    H.BindlessSlot = B.BindlessSlot;
    uint32_t Slot = B.BindlessSlot;
    H.Id = m_pool.InsertBuffer(std::move(B));
    HELIO_LOG_DEBUG("RHI", "Buffer created: id={} size={} slot={} name='{}'",
                    H.Id, Desc.Size, Slot, Desc.DebugName ? Desc.DebugName : "");
    return H;
}

void VulkanContext::DestroyBuffer(BufferHandle H) {
    if (!H.IsValid()) return;
    VulkanBuffer B = m_pool.TakeBuffer(H.Id);
    VkDevice Dev = m_device;
    VmaAllocator Alloc = m_allocator;
    VulkanBindless* Bind = m_bindless.get();
    uint32_t Slot = B.BindlessSlot;
    VkBuffer Buf = B.Buffer;
    VmaAllocation A = B.Allocation;
    bool WasStorage = HasFlag(B.Usage, BufferUsage::Storage);
    m_deletion.Push(m_frameIndex, [Dev, Alloc, Bind, Slot, Buf, A, WasStorage]() {
        if (WasStorage && Slot != UINT32_MAX) Bind->FreeStorageBuffer(Slot);
        if (Buf) vmaDestroyBuffer(Alloc, Buf, A);
        (void)Dev;
    });
}

void VulkanContext::UploadToBuffer(BufferHandle H, uint64_t Offset, const void* Data, uint64_t Size) {
    if (!H.IsValid() || Size == 0) return;
    VulkanBuffer* B = m_pool.GetBuffer(H.Id);
    HELIO_CHECK(B);
    if (B->Memory == MemoryUsage::DeviceLocal) {
        m_uploader->UploadToBuffer(B->Buffer, Offset, Data, Size);
    } else {
        HELIO_CHECK(B->MappedPtr);
        std::memcpy(static_cast<uint8_t*>(B->MappedPtr) + Offset, Data, Size);
    }
}

TextureHandle VulkanContext::CreateTexture(const TextureDesc& Desc) {
    HELIO_CHECK(Desc.Width > 0 && Desc.Height > 0 && Desc.Depth > 0);

    VulkanTexture T{};
    T.Width = Desc.Width;
    T.Height = Desc.Height;
    T.Depth = Desc.Depth;
    T.MipLevels = Desc.MipLevels;
    T.ArrayLayers = Desc.ArrayLayers;
    T.Usage = Desc.Usage;
    T.Fmt = ToVk(Desc.Fmt);

    TextureUsage UseExt = Desc.Usage;
    if (Desc.InitialData) UseExt = UseExt | TextureUsage::TransferDst;

    VkImageCreateInfo ICI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ICI.imageType = (Desc.Depth > 1) ? VK_IMAGE_TYPE_3D :
                    (Desc.Height > 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D);
    ICI.format = T.Fmt;
    ICI.extent = { Desc.Width, Desc.Height, Desc.Depth };
    ICI.mipLevels = Desc.MipLevels;
    ICI.arrayLayers = Desc.ArrayLayers;
    ICI.samples = VK_SAMPLE_COUNT_1_BIT;
    ICI.tiling = VK_IMAGE_TILING_OPTIMAL;
    ICI.usage = ToVk(UseExt);
    ICI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ICI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ACI{};
    ACI.usage = VMA_MEMORY_USAGE_AUTO;

    VK_CHECK(vmaCreateImage(m_allocator, &ICI, &ACI, &T.Image, &T.Allocation, nullptr));

    VkImageViewCreateInfo VCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VCI.image = T.Image;
    VCI.viewType = (ICI.imageType == VK_IMAGE_TYPE_3D) ? VK_IMAGE_VIEW_TYPE_3D :
                   (Desc.ArrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    VCI.format = T.Fmt;
    VCI.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    VCI.subresourceRange = { AspectFor(Desc.Fmt), 0, Desc.MipLevels, 0, Desc.ArrayLayers };
    VK_CHECK(vkCreateImageView(m_device, &VCI, nullptr, &T.View));

    if (Desc.DebugName) {
        VkDebugUtilsObjectNameInfoEXT N{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        N.objectType = VK_OBJECT_TYPE_IMAGE;
        N.objectHandle = reinterpret_cast<uint64_t>(T.Image);
        N.pObjectName = Desc.DebugName;
        if (vkSetDebugUtilsObjectNameEXT) vkSetDebugUtilsObjectNameEXT(m_device, &N);
    }

    if (Desc.InitialData) {
        m_uploader->UploadToImage(T.Image, ICI.extent,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                  Desc.InitialData, Desc.InitialDataSize);
        T.CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    if (HasFlag(Desc.Usage, TextureUsage::Sampled)) {
        T.SampledSlot = m_bindless->AllocateSampledImage();
        HELIO_CHECK(T.SampledSlot != UINT32_MAX);
        VkImageLayout L = (T.CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                          ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                          : VK_IMAGE_LAYOUT_GENERAL;
        m_bindless->WriteSampledImage(m_device, T.SampledSlot, T.View, L);
    }
    if (HasFlag(Desc.Usage, TextureUsage::Storage)) {
        T.StorageSlot = m_bindless->AllocateStorageImage();
        HELIO_CHECK(T.StorageSlot != UINT32_MAX);
        m_bindless->WriteStorageImage(m_device, T.StorageSlot, T.View);
    }

    TextureHandle H{};
    H.SampledSlot = T.SampledSlot;
    H.StorageSlot = T.StorageSlot;
    uint32_t SS = T.SampledSlot;
    uint32_t TS = T.StorageSlot;
    H.Id = m_pool.InsertTexture(std::move(T));
    HELIO_LOG_DEBUG("RHI", "Texture created: id={} {}x{} fmt={} sampled={} storage={} name='{}'",
                    H.Id, Desc.Width, Desc.Height, static_cast<int>(Desc.Fmt),
                    SS, TS, Desc.DebugName ? Desc.DebugName : "");
    return H;
}

void VulkanContext::DestroyTexture(TextureHandle H) {
    if (!H.IsValid()) return;
    VulkanTexture T = m_pool.TakeTexture(H.Id);
    VkDevice Dev = m_device;
    VmaAllocator Alloc = m_allocator;
    VulkanBindless* Bind = m_bindless.get();
    VkImage Img = T.Image;
    VkImageView View = T.View;
    VmaAllocation A = T.Allocation;
    uint32_t SS = T.SampledSlot;
    uint32_t TS = T.StorageSlot;
    m_deletion.Push(m_frameIndex, [Dev, Alloc, Bind, Img, View, A, SS, TS]() {
        if (SS != UINT32_MAX) Bind->FreeSampledImage(SS);
        if (TS != UINT32_MAX) Bind->FreeStorageImage(TS);
        if (View) vkDestroyImageView(Dev, View, nullptr);
        if (Img) vmaDestroyImage(Alloc, Img, A);
    });
}

Device::BindlessUsage VulkanContext::GetBindlessUsage() const {
    auto U = m_bindless->GetUsage();
    return { U.SampledImagesUsed, U.StorageImagesUsed, U.StorageBuffersUsed };
}

RayTracingProperties VulkanContext::GetRayTracingProperties() const {
    if (!m_hasRayTracing) return {};
    return RayTracingProperties{
        m_rtProps.maxRayRecursionDepth,
        m_rtProps.shaderGroupHandleSize,
        m_rtProps.shaderGroupHandleAlignment,
        m_rtProps.shaderGroupBaseAlignment,
        m_rtProps.maxShaderGroupStride,
        m_asProps.minAccelerationStructureScratchOffsetAlignment,
        m_asProps.maxGeometryCount,
        m_asProps.maxInstanceCount,
        m_asProps.maxPrimitiveCount,
    };
}

// =============================================================================
// Phase 6 — pipelines + draw helpers
// =============================================================================
PipelineHandle VulkanContext::CreateGraphicsPipeline(const GraphicsPipelineDesc& Desc) {
    GraphicsPipelineDesc D = Desc;
    if (D.ColorAttachmentCount > 0 && D.ColorFormats[0] == Format::Undefined) {
        // Convenience: default first color attachment to swapchain format.
        D.ColorFormats[0] = (m_swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB)
            ? Format::BGRA8_SRGB
            : Format::BGRA8_UNORM;
    }
    VulkanPipeline P = m_pipelineFactory->CreateGraphics(D);
    PipelineHandle H{ m_nextPipelineId++ };
    m_pipelines.emplace(H.Id, P);
    return H;
}

PipelineHandle VulkanContext::CreateComputePipeline(const ComputePipelineDesc& Desc) {
    VulkanPipeline P = m_pipelineFactory->CreateCompute(Desc);
    PipelineHandle H{ m_nextPipelineId++ };
    m_pipelines.emplace(H.Id, P);
    return H;
}

void VulkanContext::DestroyPipeline(PipelineHandle H) {
    if (!H.IsValid()) return;
    auto It = m_pipelines.find(H.Id);
    if (It == m_pipelines.end()) return;
    VkDevice Dev = m_device;
    VkPipeline Pipe = It->second.Pipeline;
    m_pipelines.erase(It);
    m_deletion.Push(m_frameIndex, [Dev, Pipe]() {
        if (Pipe) vkDestroyPipeline(Dev, Pipe, nullptr);
    });
}

const VulkanPipeline* VulkanContext::LookupPipeline(PipelineHandle H) const {
    auto It = m_pipelines.find(H.Id);
    return It == m_pipelines.end() ? nullptr : &It->second;
}

void VulkanContext::BeginRenderingToSwapchainInternal(VulkanCommandListImpl* C, float R, float G, float B, float A) {
    VkCommandBuffer Cmd = C->Cmd;
    VkImage Img = m_swapchainImages[m_currentImageIndex];
    VkImageView View = m_swapchainImageViews[m_currentImageIndex];

    // Transition image to COLOR_ATTACHMENT_OPTIMAL.
    VkImageMemoryBarrier2 B1{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    B1.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    B1.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    B1.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    B1.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    B1.oldLayout = m_currentImageLayout;
    B1.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    B1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    B1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    B1.image = Img;
    B1.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkDependencyInfo Dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    Dep.imageMemoryBarrierCount = 1;
    Dep.pImageMemoryBarriers = &B1;
    vkCmdPipelineBarrier2(Cmd, &Dep);

    VkRenderingAttachmentInfo Color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    Color.imageView = View;
    Color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    Color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    Color.clearValue.color.float32[0] = R;
    Color.clearValue.color.float32[1] = G;
    Color.clearValue.color.float32[2] = B;
    Color.clearValue.color.float32[3] = A;

    VkRenderingInfo Info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    Info.renderArea = { {0, 0}, m_swapchainExtent };
    Info.layerCount = 1;
    Info.colorAttachmentCount = 1;
    Info.pColorAttachments = &Color;
    vkCmdBeginRendering(Cmd, &Info);

    m_currentImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    SetViewportFullInternal(C);
}

void VulkanContext::SetViewportFullInternal(VulkanCommandListImpl* C) {
    SetViewportToExtentInternal(C, m_swapchainExtent.width, m_swapchainExtent.height);
}

void VulkanContext::SetViewportToExtentInternal(VulkanCommandListImpl* C, uint32_t Width, uint32_t Height) {
    // Standard Vulkan viewport: origin top-left, Y-down framebuffer. 2D
    // passes (overlay, fullscreen blits) author positions in this convention
    // so we keep it native. 3D passes that want world-up = screen-up handle
    // it by negating Y in their projection matrix (see PerspectiveReverseZLH),
    // and use `FrontFace::Clockwise` to compensate for the resulting flipped
    // winding classification.
    VkViewport VP{};
    VP.x = 0.0f;
    VP.y = 0.0f;
    VP.width = static_cast<float>(Width);
    VP.height = static_cast<float>(Height);
    VP.minDepth = 0.0f;
    VP.maxDepth = 1.0f;
    vkCmdSetViewport(C->Cmd, 0, 1, &VP);
    VkRect2D Scissor{ {0, 0}, { Width, Height } };
    vkCmdSetScissor(C->Cmd, 0, 1, &Scissor);
}

namespace {

VkAttachmentLoadOp ToVkLoadOp(LoadOp L) {
    switch (L) {
        case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

/// Issue a sync2 image barrier transitioning a texture between layouts.
void ImageBarrier(VkCommandBuffer Cmd, VkImage Image, VkImageAspectFlags Aspect,
                  VkImageLayout OldLayout, VkImageLayout NewLayout,
                  VkPipelineStageFlags2 SrcStage, VkAccessFlags2 SrcAccess,
                  VkPipelineStageFlags2 DstStage, VkAccessFlags2 DstAccess) {
    VkImageMemoryBarrier2 B{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    B.srcStageMask = SrcStage;  B.srcAccessMask = SrcAccess;
    B.dstStageMask = DstStage;  B.dstAccessMask = DstAccess;
    B.oldLayout = OldLayout;    B.newLayout = NewLayout;
    B.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    B.image = Image;
    B.subresourceRange = { Aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
    VkDependencyInfo Dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    Dep.imageMemoryBarrierCount = 1;
    Dep.pImageMemoryBarriers = &B;
    vkCmdPipelineBarrier2(Cmd, &Dep);
}

} // namespace

void VulkanContext::BeginRenderingToTexturesInternal(VulkanCommandListImpl* C,
                                                     const ColorAttachment* Colors, uint32_t NumColors,
                                                     const DepthAttachment* Depth) {
    HELIO_CHECK(NumColors <= 8);
    HELIO_CHECK(NumColors > 0 || Depth);

    // Build the per-attachment VkRenderingAttachmentInfo array, transitioning
    // each texture to the right layout first.
    std::array<VkRenderingAttachmentInfo, 8> ColorAttInfos{};
    uint32_t Width = 0, Height = 0;

    for (uint32_t I = 0; I < NumColors; ++I) {
        auto* Tex = m_pool.GetTexture(Colors[I].Target.Id);
        HELIO_CHECK(Tex && Tex->View);

        if (Tex->CurrentLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            ImageBarrier(C->Cmd, Tex->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                         Tex->CurrentLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            Tex->CurrentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        if (I == 0) { Width = Tex->Width; Height = Tex->Height; }
        HELIO_CHECK(Tex->Width == Width && Tex->Height == Height);

        ColorAttInfos[I] = VkRenderingAttachmentInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        ColorAttInfos[I].imageView = Tex->View;
        ColorAttInfos[I].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ColorAttInfos[I].loadOp = ToVkLoadOp(Colors[I].Load);
        ColorAttInfos[I].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        for (int K = 0; K < 4; ++K) ColorAttInfos[I].clearValue.color.float32[K] = Colors[I].ClearColor[K];
    }

    VkRenderingAttachmentInfo DepthAttInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (Depth) {
        auto* Tex = m_pool.GetTexture(Depth->Target.Id);
        HELIO_CHECK(Tex && Tex->View);

        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (Tex->Fmt == VK_FORMAT_D24_UNORM_S8_UINT) Aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

        if (Tex->CurrentLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL &&
            Tex->CurrentLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            ImageBarrier(C->Cmd, Tex->Image, Aspect,
                         Tex->CurrentLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            Tex->CurrentLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }

        if (Width == 0) { Width = Tex->Width; Height = Tex->Height; }
        DepthAttInfo.imageView = Tex->View;
        DepthAttInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        DepthAttInfo.loadOp = ToVkLoadOp(Depth->Load);
        DepthAttInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        DepthAttInfo.clearValue.depthStencil.depth = Depth->ClearDepth;
        DepthAttInfo.clearValue.depthStencil.stencil = 0;
    }

    VkRenderingInfo Info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    Info.renderArea = { {0, 0}, { Width, Height } };
    Info.layerCount = 1;
    Info.colorAttachmentCount = NumColors;
    Info.pColorAttachments = NumColors > 0 ? ColorAttInfos.data() : nullptr;
    if (Depth) Info.pDepthAttachment = &DepthAttInfo;

    vkCmdBeginRendering(C->Cmd, &Info);
    SetViewportToExtentInternal(C, Width, Height);
}

void VulkanContext::BindVertexBufferInternal(VulkanCommandListImpl* C, BufferHandle H, uint32_t Binding, uint64_t Offset) {
    auto* B = m_pool.GetBuffer(H.Id);
    HELIO_CHECK(B);
    vkCmdBindVertexBuffers(C->Cmd, Binding, 1, &B->Buffer, &Offset);
}

void VulkanContext::BindIndexBufferInternal(VulkanCommandListImpl* C, BufferHandle H, VkIndexType Type, uint64_t Offset) {
    auto* B = m_pool.GetBuffer(H.Id);
    HELIO_CHECK(B);
    vkCmdBindIndexBuffer(C->Cmd, B->Buffer, Offset, Type);
}

void VulkanContext::BlitImageInternal(VulkanCommandListImpl* C, TextureHandle SrcH, TextureHandle DstH, VkFilter Filter) {
    auto* Src = m_pool.GetTexture(SrcH.Id);
    auto* Dst = m_pool.GetTexture(DstH.Id);
    HELIO_CHECK(Src && Dst);

    if (Src->CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        ImageBarrier(C->Cmd, Src->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                     Src->CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        Src->CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (Dst->CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        ImageBarrier(C->Cmd, Dst->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                     Dst->CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        Dst->CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    VkImageBlit Region{};
    Region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.srcOffsets[0] = { 0, 0, 0 };
    Region.srcOffsets[1] = { static_cast<int32_t>(Src->Width), static_cast<int32_t>(Src->Height), 1 };
    Region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.dstOffsets[0] = { 0, 0, 0 };
    Region.dstOffsets[1] = { static_cast<int32_t>(Dst->Width), static_cast<int32_t>(Dst->Height), 1 };

    vkCmdBlitImage(C->Cmd,
                   Src->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   Dst->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &Region, Filter);
}

void VulkanContext::BlitToSwapchainInternal(VulkanCommandListImpl* C, TextureHandle SrcH, VkFilter Filter) {
    auto* Src = m_pool.GetTexture(SrcH.Id);
    HELIO_CHECK(Src);

    if (Src->CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        ImageBarrier(C->Cmd, Src->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                     Src->CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        Src->CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    // Swapchain image -> TRANSFER_DST_OPTIMAL.
    VkImage SwapImg = m_swapchainImages[m_currentImageIndex];
    VkImageMemoryBarrier2 ToDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ToDst.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ToDst.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    ToDst.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
    ToDst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    ToDst.oldLayout = m_currentImageLayout;
    ToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ToDst.image = SwapImg;
    ToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkDependencyInfo Dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    Dep.imageMemoryBarrierCount = 1;
    Dep.pImageMemoryBarriers = &ToDst;
    vkCmdPipelineBarrier2(C->Cmd, &Dep);
    m_currentImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkImageBlit Region{};
    Region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.srcOffsets[0] = { 0, 0, 0 };
    Region.srcOffsets[1] = { static_cast<int32_t>(Src->Width), static_cast<int32_t>(Src->Height), 1 };
    Region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.dstOffsets[0] = { 0, 0, 0 };
    Region.dstOffsets[1] = { static_cast<int32_t>(m_swapchainExtent.width),
                             static_cast<int32_t>(m_swapchainExtent.height), 1 };
    vkCmdBlitImage(C->Cmd,
                   Src->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   SwapImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &Region, Filter);
}

void VulkanContext::CopyImageInternal(VulkanCommandListImpl* C, TextureHandle SrcH, TextureHandle DstH) {
    auto* Src = m_pool.GetTexture(SrcH.Id);
    auto* Dst = m_pool.GetTexture(DstH.Id);
    HELIO_CHECK(Src && Dst);
    HELIO_CHECK(Src->Width == Dst->Width && Src->Height == Dst->Height && Src->Fmt == Dst->Fmt);

    if (Src->CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        ImageBarrier(C->Cmd, Src->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                     Src->CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        Src->CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (Dst->CurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        ImageBarrier(C->Cmd, Dst->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                     Dst->CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        Dst->CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    VkImageCopy Region{};
    Region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    Region.extent = { Src->Width, Src->Height, 1 };
    vkCmdCopyImage(C->Cmd,
                   Src->Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   Dst->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &Region);
}

// =============================================================================
// Phase 8 — accel structures
// =============================================================================
BLASHandle VulkanContext::BuildBLAS(const BLASDesc& Desc) {
    HELIO_CHECK(m_accelBuilder);
    VulkanBLAS B = m_accelBuilder->BuildBLAS(Desc);
    BLASHandle H{ m_nextAccelId++ };
    m_blas.emplace(H.Id, B);
    return H;
}

void VulkanContext::DestroyBLAS(BLASHandle H) {
    if (!H.IsValid()) return;
    auto It = m_blas.find(H.Id);
    if (It == m_blas.end()) return;
    VulkanBLAS B = It->second;
    m_blas.erase(It);
    VkDevice Dev = m_device;
    VmaAllocator Alloc = m_allocator;
    m_deletion.Push(m_frameIndex, [Dev, Alloc, B]() {
        if (B.Accel) vkDestroyAccelerationStructureKHR(Dev, B.Accel, nullptr);
        if (B.Storage) vmaDestroyBuffer(Alloc, B.Storage, B.StorageAlloc);
    });
}

TLASHandle VulkanContext::BuildTLAS(const TLASDesc& Desc) {
    HELIO_CHECK(m_accelBuilder);
    std::vector<VkDeviceAddress> Addrs(Desc.InstanceCount);
    for (uint32_t I = 0; I < Desc.InstanceCount; ++I) {
        auto It = m_blas.find(Desc.Instances[I].BLAS.Id);
        HELIO_CHECK(It != m_blas.end());
        Addrs[I] = It->second.DeviceAddress;
    }
    VulkanTLAS T = m_accelBuilder->BuildTLAS(Desc, Addrs);
    TLASHandle H{ m_nextAccelId++ };
    m_tlas.emplace(H.Id, T);
    return H;
}

void VulkanContext::DestroyTLAS(TLASHandle H) {
    if (!H.IsValid()) return;
    auto It = m_tlas.find(H.Id);
    if (It == m_tlas.end()) return;
    VulkanTLAS T = It->second;
    m_tlas.erase(It);
    VkDevice Dev = m_device;
    VmaAllocator Alloc = m_allocator;
    m_deletion.Push(m_frameIndex, [Dev, Alloc, T]() {
        if (T.Accel) vkDestroyAccelerationStructureKHR(Dev, T.Accel, nullptr);
        if (T.Storage) vmaDestroyBuffer(Alloc, T.Storage, T.StorageAlloc);
        if (T.InstanceBuf) vmaDestroyBuffer(Alloc, T.InstanceBuf, T.InstanceAlloc);
    });
}

void VulkanContext::SetActiveTLAS(TLASHandle H) {
    m_activeTlas = H;
    if (!H.IsValid()) return;
    auto It = m_tlas.find(H.Id);
    HELIO_CHECK(It != m_tlas.end());
    m_bindless->WriteTLAS(m_device, It->second.Accel);
}

TLASHandle VulkanContext::BuildVerificationTLAS() {
    HELIO_CHECK(m_accelBuilder);

    // Unit triangle in the XY plane at z = 0.
    static const float kVerts[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };
    static const uint32_t kIdx[] = { 0, 1, 2 };

    BufferHandle VB = CreateBuffer({
        .Size = sizeof(kVerts),
        .Usage = BufferUsage::AccelStructureBuild | BufferUsage::ShaderDeviceAddress,
        .Memory = MemoryUsage::DeviceLocal,
        .DebugName = "Verify.VB",
        .InitialData = kVerts,
        .InitialDataSize = sizeof(kVerts),
    });
    BufferHandle IB = CreateBuffer({
        .Size = sizeof(kIdx),
        .Usage = BufferUsage::AccelStructureBuild | BufferUsage::ShaderDeviceAddress,
        .Memory = MemoryUsage::DeviceLocal,
        .DebugName = "Verify.IB",
        .InitialData = kIdx,
        .InitialDataSize = sizeof(kIdx),
    });

    BLASGeometry Geom{};
    Geom.Vertices = VB;
    Geom.VertexStride = sizeof(float) * 3;
    Geom.VertexCount = 3;
    Geom.VertexFormat = Format::RGB32F;
    Geom.Indices = IB;
    Geom.IndexFormat = 1; // U32
    Geom.IndexCount = 3;
    Geom.Opaque = true;

    BLASDesc BD{};
    BD.Geometries = &Geom;
    BD.GeometryCount = 1;
    BD.DebugName = "Verify.BLAS";
    BLASHandle BlasH = BuildBLAS(BD);

    TLASInstance Inst{};
    Inst.BLAS = BlasH;
    // Identity transform default is already set in TLASInstance.

    TLASDesc TD{};
    TD.Instances = &Inst;
    TD.InstanceCount = 1;
    TD.DebugName = "Verify.TLAS";
    TLASHandle TlasH = BuildTLAS(TD);

    SetActiveTLAS(TlasH);
    HELIO_LOG_INFO("RHI", "BuildVerificationTLAS: BLAS={} TLAS={} (active)", BlasH.Id, TlasH.Id);
    return TlasH;
}

void VulkanContext::TransitionForStorageWriteInternal(VulkanCommandListImpl* C, TextureHandle H) {
    auto* Tex = m_pool.GetTexture(H.Id);
    HELIO_CHECK(Tex);
    if (Tex->CurrentLayout == VK_IMAGE_LAYOUT_GENERAL) return;
    ImageBarrier(C->Cmd, Tex->Image, VK_IMAGE_ASPECT_COLOR_BIT,
                 Tex->CurrentLayout, VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    Tex->CurrentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void VulkanContext::TransitionForSamplingInternal(VulkanCommandListImpl* C, TextureHandle H) {
    auto* Tex = m_pool.GetTexture(H.Id);
    HELIO_CHECK(Tex);
    if (Tex->CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return;

    VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkPipelineStageFlags2 SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    if (Tex->Fmt == VK_FORMAT_D32_SFLOAT || Tex->Fmt == VK_FORMAT_D24_UNORM_S8_UINT) {
        Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (Tex->Fmt == VK_FORMAT_D24_UNORM_S8_UINT) Aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        SrcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    ImageBarrier(C->Cmd, Tex->Image, Aspect,
                 Tex->CurrentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 SrcStage, SrcAccess,
                 VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    Tex->CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // If this texture is bound at a sampled slot, the descriptor was written
    // at create time pointing at SHADER_READ_ONLY_OPTIMAL — no rewrite needed.
}

} // namespace helio::rhi::vulkan
