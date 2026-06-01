/// @file Device.h
/// @brief Public RHI device interface — back-end agnostic.
///
/// The concrete implementation lives in `RHI/Vulkan/VulkanContext.{h,cpp}`.
/// Game code only includes this header; no Vulkan symbols leak.
///
/// V1 surface grows phase-by-phase:
/// - Phase 4: Init + clear-current-image + frame loop
/// - Phase 5: CreateBuffer / CreateTexture + bindless slots + upload
/// - Phase 6: Pipelines
/// - Phase 7: CommandList
/// - Phase 8: AccelStructure
#pragma once

#include <RHI/Public/Buffer.h>
#include <RHI/Public/Texture.h>
#include <RHI/Public/Pipeline.h>
#include <RHI/Public/CommandList.h>
#include <RHI/Public/AccelStructure.h>

#include <memory>

namespace helio::rhi {

struct DeviceConfig {
    /// SDL_Window* cast to void* — Device internally calls SDL_Vulkan_CreateSurface.
    void* NativeWindow{nullptr};
    int InitialWidth{1280};
    int InitialHeight{720};
    bool EnableValidation{true};
    /// REQUEST ray tracing. If no Vulkan 1.3 GPU on this system also exposes
    /// the four RT extensions (acceleration structure, RT pipeline, ray query,
    /// deferred host operations), the device falls back to non-RT and logs a
    /// warning. Query `Device::HasRayTracing()` post-construction to learn
    /// whether RT actually came up.
    bool EnableRayTracing{true};
};

/// Hardware ray-tracing properties. All zero when `Device::HasRayTracing()` is false.
struct RayTracingProperties {
    /// Highest legal `traceRayEXT()` recursion depth supported by the GPU.
    uint32_t MaxRayRecursionDepth;
    /// Size of a single shader-group handle (SBT entry payload).
    uint32_t ShaderGroupHandleSize;
    /// Alignment requirement for individual SBT handles.
    uint32_t ShaderGroupHandleAlignment;
    /// Alignment requirement for SBT base addresses (per-stage region start).
    uint32_t ShaderGroupBaseAlignment;
    /// Maximum stride between SBT entries.
    uint32_t MaxShaderGroupStride;
    /// Acceleration-structure scratch buffer offset alignment.
    uint32_t MinAccelStructScratchOffsetAlignment;
    /// Limits on BLAS / TLAS sizes.
    uint64_t MaxGeometryCount;
    uint64_t MaxInstanceCount;
    uint64_t MaxPrimitiveCount;
};

class Device {
public:
    explicit Device(const DeviceConfig& Config);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    /// Acquire the next swapchain image and begin recording the frame's
    /// command buffer.
    ///
    /// Returns a non-null `CommandList*` pointing at the frame's recording
    /// surface on success. Returns `nullptr` if the swapchain was out of date
    /// and got recreated — skip rendering this frame and try again next loop.
    ///
    /// The returned pointer is owned by the Device and is only valid until
    /// `EndFrame()` returns. Do not cache it across frames.
    [[nodiscard]] CommandList* BeginFrame();

    /// End the frame's command buffer, submit, present, and rotate the
    /// frame-in-flight index. Always pair with a successful `BeginFrame()`.
    void EndFrame();

    /// Block until the GPU is fully idle (use sparingly — call before
    /// destructive ops like resize or shutdown).
    void WaitIdle();

    /// Recreate the swapchain at the new size. Call from your resize handler.
    void Resize(int Width, int Height);

    // -------------------------------------------------------------------------
    // Phase 5 — resources
    // -------------------------------------------------------------------------

    /// Create a buffer. If `Desc.InitialData` is non-null, the data is
    /// uploaded synchronously via an internal staging buffer before returning.
    [[nodiscard]] BufferHandle CreateBuffer(const BufferDesc& Desc);

    /// Schedule destruction. The actual `vkDestroy*` runs after the GPU has
    /// retired the frame using this resource (deletion queue, ~2 frames later).
    void DestroyBuffer(BufferHandle Handle);

    /// Upload `Size` bytes from `Data` into the buffer at `Offset`. For
    /// DeviceLocal buffers, goes via staging + transfer queue submit (synchronous).
    /// For HostUpload buffers, writes directly to the mapped pointer.
    void UploadToBuffer(BufferHandle Handle, uint64_t Offset, const void* Data, uint64_t Size);

    /// Create a texture. Honors Sampled/Storage usage by allocating bindless
    /// slots. Initial data (if any) is uploaded mip 0 / layer 0 tightly packed.
    [[nodiscard]] TextureHandle CreateTexture(const TextureDesc& Desc);

    void DestroyTexture(TextureHandle Handle);

    /// Counts of bindless slots currently allocated (debug stat).
    struct BindlessUsage {
        uint32_t SampledImagesUsed;
        uint32_t StorageImagesUsed;
        uint32_t StorageBuffersUsed;
    };
    [[nodiscard]] BindlessUsage GetBindlessUsage() const;

    // -------------------------------------------------------------------------
    // Phase 6 — pipelines + minimal draw helpers
    // -------------------------------------------------------------------------

    [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& Desc);
    [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDesc& Desc);
    void DestroyPipeline(PipelineHandle H);

    // -------------------------------------------------------------------------
    // Phase 8 — raytracing acceleration structures
    //
    // Workflow:
    //   1. Create vertex/index buffers with `BufferUsage::AccelStructureBuild`.
    //   2. Build a BLAS per static mesh once at load time.
    //   3. Build a TLAS from BLAS instances each frame (or on scene change).
    //   4. `SetActiveTLAS(handle)` writes it into bindless slot 4 so shaders
    //      can call `GetTLAS()` from `Bindless.slang`.
    //   5. Compute / fragment / RT-pipeline shaders use ray queries or
    //      traceRaysKHR against the bound TLAS.
    //
    // All accel-structure operations require `HasRayTracing() == true`.
    // -------------------------------------------------------------------------

    [[nodiscard]] BLASHandle BuildBLAS(const BLASDesc& Desc);
    void DestroyBLAS(BLASHandle H);

    [[nodiscard]] TLASHandle BuildTLAS(const TLASDesc& Desc);
    void DestroyTLAS(TLASHandle H);

    /// Bind a TLAS at the bindless slot reserved at binding 4. Shaders read
    /// it via `GetTLAS()`. Pass an invalid handle to unbind (a null TLAS
    /// is what the slot starts as — every `traceRay*` returns "miss").
    void SetActiveTLAS(TLASHandle H);

    /// Convenience: build a single-triangle BLAS + TLAS at world origin
    /// and set it as the active TLAS. Returns the handle so the caller can
    /// keep it alive (it joins the deletion queue on destruction).
    ///
    /// The triangle vertices are `(0, 0.5, 0)`, `(-0.5, -0.5, 0)`,
    /// `(0.5, -0.5, 0)` — i.e. a unit triangle in the XY plane at z=0.
    /// Useful to verify ray-query plumbing without needing a mesh loader.
    [[nodiscard]] TLASHandle BuildVerificationTLAS();

    // -------------------------------------------------------------------------
    // Capabilities
    // -------------------------------------------------------------------------

    /// True if the selected physical device exposes the full RT extension set
    /// (acceleration structure + RT pipeline + ray query + deferred host ops)
    /// AND we successfully enabled them on the logical device.
    ///
    /// Use this to gate any code path that calls into RT (BLAS/TLAS builds,
    /// `traceRayEXT`, SBT setup). Reflects actual hardware state, not the
    /// `DeviceConfig::EnableRayTracing` request.
    [[nodiscard]] bool HasRayTracing() const;

    /// Cached RT + acceleration-structure limits. All fields are zero when
    /// `HasRayTracing()` is false — branch on that first.
    [[nodiscard]] RayTracingProperties GetRayTracingProperties() const;

    /// Wall-clock GPU duration of the most recently-retired frame, in
    /// milliseconds. Backed by `VkCmdWriteTimestamp2` at the top of each
    /// frame's command buffer and the bottom of pre-present, read back two
    /// frames later (so the value lags ~2 frames behind the CPU).
    ///
    /// Returns `0.0` until at least `FramesInFlight` frames have completed.
    [[nodiscard]] double LastFrameGpuMs() const;

    /// Number of frames the GPU pipeline allows in flight concurrently.
    /// Useful for sizing per-frame ring buffers. V1: always 2.
    [[nodiscard]] uint32_t FramesInFlight() const noexcept;

    /// Index of the frame slot currently being recorded (0 .. FramesInFlight-1).
    /// Rotates on every `EndFrame`. Use with `RingUploadBuffer` and any other
    /// per-frame resource that needs to avoid being overwritten while the GPU
    /// is still reading the previous frame.
    [[nodiscard]] uint32_t CurrentFrameIndex() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace helio::rhi
