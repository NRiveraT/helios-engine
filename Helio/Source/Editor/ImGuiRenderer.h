/// @file ImGuiRenderer.h
/// @brief Renders Dear ImGui draw data through Helio's bindless RHI.
///
/// This is a from-scratch renderer backend (no imgui_impl_vulkan): vertices
/// and u16 indices are streamed into per-frame `RingUploadBuffer`s and pulled
/// bindlessly in `Shaders/Editor/ImGuiPass.slang` — the exact pattern every
/// other Helio pass uses, so the RHI's Vulkan encapsulation stays intact.
/// One non-indexed draw per ImDrawCmd with its scissor rect; alpha blending
/// via `BlendMode::Alpha`.
///
/// The font atlas is uploaded once at construction through the normal
/// bindless texture path; its slot is handed to ImGui as the ImTextureID.
#pragma once

#include <Renderer/RenderGraph.h>

#include <RHI/Public/Device.h>
#include <RHI/Public/RingUploadBuffer.h>

#include <cstdint>

struct ImDrawData;

namespace helio::editor
{
    /// CPU mirror of the PC block in `Shaders/Editor/ImGuiPass.slang`.
    struct ImGuiPushConsts
    {
        float Scale[2];     //  0
        float Translate[2]; //  8

        uint32_t VertexBufferSlot; // 16
        uint32_t IndexBufferSlot;  // 20
        uint32_t TextureSlot;      // 24
        uint32_t VertexOffset;     // 28

        uint32_t IndexOffset;      // 32
        uint32_t Pad0, Pad1, Pad2;
    };
    static_assert(sizeof(ImGuiPushConsts) == 48,
                  "must match the PC block in Shaders/Editor/ImGuiPass.slang");

    class ImGuiRenderer
    {
    public:
        /// `TargetFormat` must match the color target the UI pass draws onto.
        /// Call AFTER ImGui::CreateContext (the font atlas is built here).
        ImGuiRenderer(rhi::Device& Dev, rhi::Format TargetFormat);
        ~ImGuiRenderer();

        ImGuiRenderer(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

        /// Declare the UI pass onto `Target` (LoadOp::Load — draws over the
        /// scene). `DrawData` is consumed at declaration time (commands are
        /// copied, buffers uploaded); it does not need to outlive this call.
        void Render(render::RenderGraph& Rg, rhi::TextureHandle Target,
                    ImDrawData* DrawData, uint32_t Width, uint32_t Height);

    private:
        // Per-frame-slot streaming capacity. UI worst cases are tens of
        // thousands of vertices; these bounds fit ~52k verts / ~256k indices.
        // Overflow drops the UI for that frame with a rate-limited warning.
        static constexpr uint64_t kVertexBytesPerFrame = 1u << 20; // 1 MiB
        static constexpr uint64_t kIndexBytesPerFrame = 512u << 10; // 512 KiB

        rhi::Device* m_Dev;
        rhi::PipelineHandle m_Pipeline;
        rhi::TextureHandle m_FontTexture;
        rhi::RingUploadBuffer m_VertexRing;
        rhi::RingUploadBuffer m_IndexRing;
        bool m_WarnedOverflow = false;
    };
} // namespace helio::editor
