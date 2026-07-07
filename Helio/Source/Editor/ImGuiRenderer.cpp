#include "ImGuiRenderer.h"

#include <Core/Logging/Log.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace helio::editor
{
    ImGuiRenderer::ImGuiRenderer(rhi::Device& Dev, rhi::Format TargetFormat)
        : m_Dev(&Dev)
        , m_VertexRing(Dev, kVertexBytesPerFrame, "ImGui.Vertices")
        , m_IndexRing(Dev, kIndexBytesPerFrame, "ImGui.Indices")
    {
        m_Pipeline = m_Dev->CreateGraphicsPipeline({
            .ShaderPath = "Shaders/Editor/ImGuiPass.spv",
            .ColorFormats = {TargetFormat},
            .ColorAttachmentCount = 1,
            .Cull = rhi::CullMode::None,
            .Blend = rhi::BlendMode::Alpha,
            .PushConstantBytes = sizeof(ImGuiPushConsts),
            .DebugName = "ImGui"
        });

        ImGuiIO& IO = ImGui::GetIO();
        IO.BackendRendererName = "helio_rhi_bindless";
        // The shader indexes `VertexOffset + idx`, so per-command VtxOffset is
        // fully supported — declaring it lets ImGui emit single large meshes
        // instead of splitting draw lists at 64k vertices.
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

        // Font atlas: built once, uploaded through the normal bindless path.
        // The sampled slot doubles as the ImTextureID ImGui hands back per
        // draw command.
        unsigned char* Pixels = nullptr;
        int W = 0, H = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &W, &H);
        m_FontTexture = m_Dev->CreateTexture({
            .Width = static_cast<uint32_t>(W),
            .Height = static_cast<uint32_t>(H),
            .Fmt = rhi::Format::RGBA8_UNORM,
            .Usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDst,
            .DebugName = "ImGui.FontAtlas",
            .InitialData = Pixels,
            .InitialDataSize = static_cast<uint64_t>(W) * H * 4,
        });
        IO.Fonts->SetTexID(static_cast<ImTextureID>(m_FontTexture.SampledSlot));
    }

    ImGuiRenderer::~ImGuiRenderer()
    {
        m_Dev->DestroyTexture(m_FontTexture);
        m_Dev->DestroyPipeline(m_Pipeline);
    }

    void ImGuiRenderer::Render(render::RenderGraph& Rg, rhi::TextureHandle Target, ImDrawData* DrawData, uint32_t Width, uint32_t Height)
    {
        if (DrawData == nullptr || DrawData->CmdListsCount == 0 ||
            DrawData->DisplaySize.x <= 0.0f || DrawData->DisplaySize.y <= 0.0f)
        {
            return;
        }

        const uint64_t VtxBytes = static_cast<uint64_t>(DrawData->TotalVtxCount) * sizeof(ImDrawVert);
        const uint64_t IdxBytes = static_cast<uint64_t>(DrawData->TotalIdxCount) * sizeof(ImDrawIdx);
        static_assert(sizeof(ImDrawIdx) == 2, "ImGuiPass.slang pulls u16 indices");
        static_assert(sizeof(ImDrawVert) == 20, "ImGuiPass.slang assumes 20-byte ImDrawVert");

        if (VtxBytes > m_VertexRing.Size() || IdxBytes > m_IndexRing.Size())
        {
            if (!m_WarnedOverflow)
            {
                HELIO_LOG_WARN("Editor", "ImGui draw data exceeds ring capacity ({} vtx B, {} idx B) — UI skipped", VtxBytes, IdxBytes);
                m_WarnedOverflow = true;
            }
            return;
        }

        // Upload every command list back-to-back and flatten the draw
        // commands into self-contained records — the pass lambda must not
        // touch ImGui state (it runs later, inside RenderGraph::Execute).
        struct DrawRecord
        {
            int32_t ScissorX, ScissorY;
            uint32_t ScissorW, ScissorH;
            uint32_t ElemCount;
            uint32_t VertexOffset;
            uint32_t IndexOffset;
            uint32_t TextureSlot;
        };
        std::vector<DrawRecord> Records;
        Records.reserve(64);

        const ImVec2 ClipOff = DrawData->DisplayPos;
        // Clip rects are in ImGui display units (window pixels). Scale them to
        // the render-target's pixels so scissors stay correct even if the
        // target extent differs from the window (HiDPI, or a frame mid-resize
        // before the offscreen target caught up). Vertices already map through
        // NDC (Scale = 2/DisplaySize) so they need no adjustment.
        const float ScaleX = static_cast<float>(Width) / DrawData->DisplaySize.x;
        const float ScaleY = static_cast<float>(Height) / DrawData->DisplaySize.y;
        uint32_t BaseVtx = 0;
        uint32_t BaseIdx = 0;
        for (int L = 0; L < DrawData->CmdListsCount; ++L)
        {
            const ImDrawList* List = DrawData->CmdLists[L];
            m_VertexRing.Write(static_cast<uint64_t>(BaseVtx) * sizeof(ImDrawVert), List->VtxBuffer.Data, static_cast<uint64_t>(List->VtxBuffer.Size) * sizeof(ImDrawVert));
            m_IndexRing.Write(static_cast<uint64_t>(BaseIdx) * sizeof(ImDrawIdx), List->IdxBuffer.Data, static_cast<uint64_t>(List->IdxBuffer.Size) * sizeof(ImDrawIdx));

            for (const ImDrawCmd& Cmd : List->CmdBuffer)
            {
                if (Cmd.UserCallback != nullptr)
                {
                    // User callbacks manipulate raw ImGui state; unsupported
                    // in the deferred pass model. None of Helio's UI uses them.
                    continue;
                }

                // Clip rect -> target pixels (display units * target/display),
                // clamped to the target.
                float X0 = (Cmd.ClipRect.x - ClipOff.x) * ScaleX;
                float Y0 = (Cmd.ClipRect.y - ClipOff.y) * ScaleY;
                float X1 = (Cmd.ClipRect.z - ClipOff.x) * ScaleX;
                float Y1 = (Cmd.ClipRect.w - ClipOff.y) * ScaleY;
                X0 = std::max(X0, 0.0f);
                Y0 = std::max(Y0, 0.0f);
                X1 = std::min(X1, static_cast<float>(Width));
                Y1 = std::min(Y1, static_cast<float>(Height));
                if (X1 <= X0 || Y1 <= Y0 || Cmd.ElemCount == 0)
                {
                    continue;
                }

                DrawRecord R{};
                R.ScissorX = static_cast<int32_t>(X0);
                R.ScissorY = static_cast<int32_t>(Y0);
                R.ScissorW = static_cast<uint32_t>(X1 - X0);
                R.ScissorH = static_cast<uint32_t>(Y1 - Y0);
                R.ElemCount = Cmd.ElemCount;
                R.VertexOffset = BaseVtx + Cmd.VtxOffset;
                R.IndexOffset = BaseIdx + Cmd.IdxOffset;
                R.TextureSlot = static_cast<uint32_t>(Cmd.GetTexID());
                Records.push_back(R);
            }

            BaseVtx += static_cast<uint32_t>(List->VtxBuffer.Size);
            BaseIdx += static_cast<uint32_t>(List->IdxBuffer.Size);
        }

        if (Records.empty())
        {
            return;
        }

        ImGuiPushConsts Base{};
        Base.Scale[0] = 2.0f / DrawData->DisplaySize.x;
        Base.Scale[1] = 2.0f / DrawData->DisplaySize.y;
        Base.Translate[0] = -1.0f - ClipOff.x * Base.Scale[0];
        Base.Translate[1] = -1.0f - ClipOff.y * Base.Scale[1];
        Base.VertexBufferSlot = m_VertexRing.Current().BindlessSlot;
        Base.IndexBufferSlot = m_IndexRing.Current().BindlessSlot;

        Rg.Graphics("Editor.ImGui")
          .ColorLoad(Target)
          .Execute([this, Base, Records = std::move(Records), Width, Height](rhi::CommandList& C) mutable
          {
              C.Bind(m_Pipeline);
              C.SetViewport(Width, Height); // also resets scissor to full target

              for (const DrawRecord& R : Records)
              {
                  ImGuiPushConsts PC = Base;
                  PC.TextureSlot = R.TextureSlot;
                  PC.VertexOffset = R.VertexOffset;
                  PC.IndexOffset = R.IndexOffset;

                  C.SetScissor(R.ScissorX, R.ScissorY, R.ScissorW, R.ScissorH);
                  C.Push(PC);
                  C.Draw(R.ElemCount);
              }

              C.SetScissor(0, 0, Width, Height); // leave state sane for later passes
          });
    }
} // namespace helio::editor
