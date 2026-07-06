/// @file CommandList.h
/// @brief Frame-scoped command recording surface.
///
/// `Device::BeginFrame()` returns a `CommandList*` pointing at the current
/// frame's recording buffer. Call `Device::EndFrame()` to submit + present.
/// The returned pointer is owned by the Device — do NOT cache it across frames.
///
/// All Helio pipelines share one bindless descriptor set and a single
/// push-constant range, both of which are bound automatically when you call
/// `Bind(pipeline)`. The game side never touches Vulkan types directly.
///
/// CPU Tracy zones are available everywhere via `HELIO_PROFILE_ZONE("Name")`.
/// Frame-level GPU timing shows up in Tracy automatically once the device is
/// up (the Tracy Vulkan context is created in VulkanContext init). Per-pass
/// GPU zone macros (`HELIO_GPU_ZONE(cmd, "Name")`) land in Phase 13 polish.
#pragma once

#include <RHI/Public/Buffer.h>
#include <RHI/Public/Pipeline.h>
#include <RHI/Public/Texture.h>

#include <cstdint>
#include <initializer_list>

namespace helio::rhi {

enum class LoadOp : uint8_t {
    Clear,     ///< Clear the attachment to the value below at pass start.
    Load,      ///< Keep the existing contents (read previous pass's output).
    DontCare,  ///< Contents are undefined at pass start (cheapest).
};

enum class IndexType : uint8_t {
    U16,
    U32,
};

enum class BlitFilter : uint8_t {
    Nearest,
    Linear,
};

struct ColorAttachment {
    TextureHandle Target{};
    LoadOp Load{LoadOp::Clear};
    float ClearColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachment {
    TextureHandle Target{};
    LoadOp Load{LoadOp::Clear};
    /// 0.0 for reverse-Z (far plane), 1.0 for traditional Z (near plane).
    float ClearDepth{0.0f};
};

class CommandList {
public:
    // -------------------------------------------------------------------------
    // Render-target scoping (Phase 7 surface — Phase 9 will wrap this in a
    // declarative render-graph layer that handles barriers automatically).
    // -------------------------------------------------------------------------

    /// Open a dynamic-rendering pass onto the current swapchain image with a
    /// clear color. Pair with EndRendering(). Single color attachment, no depth.
    void BeginRenderingToSwapchain(float R, float G, float B, float A);

    /// Open a dynamic-rendering pass onto one or more user-created textures.
    /// Up to 8 color attachments + optional depth. Each `Target` must have
    /// been created with `TextureUsage::ColorAttachment` (or `DepthStencilAttachment`).
    /// All color attachments must share the same width/height.
    ///
    /// Textures are auto-transitioned into the right layout for rendering.
    /// To sample them in a later pass, call `TransitionForSampling()` after
    /// `EndRendering()`.
    void BeginRendering(const ColorAttachment* Colors, uint32_t NumColors,
                        const DepthAttachment* Depth = nullptr);

    /// `initializer_list` convenience: `Cmd->BeginRendering({a, b, c});`
    void BeginRendering(std::initializer_list<ColorAttachment> Colors,
                        const DepthAttachment* Depth = nullptr);

    /// End the current rendering scope. Mandatory before submitting more
    /// command operations or starting another `BeginRendering*`.
    void EndRendering();

    /// Emit a memory barrier that transitions `Tex` from its current layout
    /// (typically COLOR_ATTACHMENT_OPTIMAL after a render pass) to
    /// SHADER_READ_ONLY_OPTIMAL, with the right access/stage masks so a
    /// subsequent shader pass can sample it via its bindless slot.
    ///
    /// Call once per texture between the producer's `EndRendering()` and the
    /// consumer's `BeginRendering*()`. No-op if the texture is already in
    /// a sampleable layout.
    void TransitionForSampling(TextureHandle Tex);

    // -------------------------------------------------------------------------
    // Pipeline + state.
    // -------------------------------------------------------------------------

    /// Bind a pipeline. Also binds the global bindless set in the same call.
    void Bind(PipelineHandle H);

    /// Upload a push-constant blob (≤ pipeline's `PushConstantBytes`).
    void Push(const void* Data, uint32_t Size);

    /// Typed convenience: `cmd.Push(myStruct);`.
    template <typename T>
    void Push(const T& Value) { Push(&Value, sizeof(T)); }

    /// Set viewport + scissor to the full swapchain extent.
    void SetViewportFull();

    /// Set viewport + scissor to a custom `Width x Height` rectangle starting
    /// at (0, 0). Use when rendering to a non-swapchain target whose extent
    /// differs from the swapchain — e.g. a 1024×1024 shadow-map texture, an
    /// offscreen RT, or a half-res buffer.
    void SetViewport(uint32_t Width, uint32_t Height);

    /// Set only the scissor rectangle, leaving the viewport untouched. UI
    /// passes (Dear ImGui) clip each draw command with this. Coordinates are
    /// framebuffer pixels, origin top-left.
    void SetScissor(int32_t X, int32_t Y, uint32_t Width, uint32_t Height);

    // -------------------------------------------------------------------------
    // Submit a draw / dispatch.
    // -------------------------------------------------------------------------

    void Draw(uint32_t VertexCount, uint32_t InstanceCount = 1,
              uint32_t FirstVertex = 0, uint32_t FirstInstance = 0);
    void DrawIndexed(uint32_t IndexCount, uint32_t InstanceCount = 1,
                     uint32_t FirstIndex = 0, int32_t VertexOffset = 0, uint32_t FirstInstance = 0);
    void Dispatch(uint32_t GroupsX, uint32_t GroupsY = 1, uint32_t GroupsZ = 1);
    /// Convenience for compute passes: divides `Size` by `Group`, rounding up.
    void Dispatch2D(uint32_t SizeX, uint32_t SizeY, uint32_t GroupX, uint32_t GroupY);

    // -------------------------------------------------------------------------
    // Buffer binding for traditional draw paths.
    //
    // Bindless / programmable vertex pulling is still the preferred V1 pattern
    // (push the buffer's `BindlessSlot` and load with `ByteAddressBuffer`),
    // but these are here for loaders that produce VkBuffer-style mesh data
    // (e.g. fastgltf output piped through meshoptimizer).
    // -------------------------------------------------------------------------

    void BindVertexBuffer(BufferHandle Buf, uint32_t Binding = 0, uint64_t Offset = 0);
    void BindIndexBuffer(BufferHandle Buf, IndexType Type, uint64_t Offset = 0);

    // -------------------------------------------------------------------------
    // Image copies / blits — for mip generation, downsample chains, screenshots,
    // and shader-free fullscreen passthroughs.
    // -------------------------------------------------------------------------

    /// Hardware blit `Src` into `Dst` with filtering. Transitions `Src` to
    /// TRANSFER_SRC and `Dst` to TRANSFER_DST for the blit and LEAVES them in
    /// those transfer layouts. To sample either afterward, call
    /// `TransitionForSampling` on it first (or declare it as a `.Read()` in the
    /// render graph, which does that for you). Source and destination extents
    /// come from the textures' descriptors.
    void BlitImage(TextureHandle Src, TextureHandle Dst, BlitFilter Filter = BlitFilter::Linear);

    /// Bit-exact copy. Src and Dst must have the same dimensions + format.
    void CopyImage(TextureHandle Src, TextureHandle Dst);

    /// Hardware blit `Src` to the current frame's swapchain image. Handles all
    /// layout transitions (Src→TRANSFER_SRC, swapchain→TRANSFER_DST, swapchain
    /// → PRESENT_SRC at EndFrame). Stretches to the full swapchain extent.
    /// Used by the render graph's `Present()` to display a final-pass output.
    void BlitToSwapchain(TextureHandle Src, BlitFilter Filter = BlitFilter::Linear);

    // -------------------------------------------------------------------------
    // Resource state transitions (Phase 9's render graph will do these auto;
    // for now you call them between writer and reader).
    // -------------------------------------------------------------------------

    /// Transition `Tex` to a layout suitable for compute storage-image writes
    /// (GENERAL). Required before binding the texture's `StorageSlot` in a
    /// compute pass that writes to it.
    void TransitionForStorageWrite(TextureHandle Tex);

    // -------------------------------------------------------------------------
    // Internals — used by VulkanContext, not by game code.
    // -------------------------------------------------------------------------

    void SetImpl(void* Impl) noexcept { m_impl = Impl; }
    [[nodiscard]] void* Impl() const noexcept { return m_impl; }

private:
    /// Opaque pointer to the backend's per-frame recording state
    /// (`helio::rhi::vulkan::VulkanCommandListImpl*`).
    void* m_impl{nullptr};
};

} // namespace helio::rhi
