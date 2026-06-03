#include "RenderGraph.h"

#include <RHI/Public/Device.h>

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>
#include <Core/Profile/Profile.h>

namespace helio::render
{
    // =============================================================================
    // PassBuilder
    // =============================================================================
    PassBuilder& PassBuilder::Read(TextureHandle h)
    {
        m_graph->m_passes[m_pass].Uses.push_back({h, Access::Read});
        return *this;
    }

    // Compute Write
    PassBuilder& PassBuilder::Write(TextureHandle h)
    {
        m_graph->m_passes[m_pass].Uses.push_back({h, Access::Write});
        return *this;
    }

    // Normal color pass
    PassBuilder& PassBuilder::Color(TextureHandle h)
    {
        return Color(h, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    PassBuilder& PassBuilder::Color(TextureHandle h, float r, float g, float b, float a)
    {
        ResourceUse U{};
        U.Handle = h;
        U.Mode = Access::Color;
        U.ClearColor[0] = r;
        U.ClearColor[1] = g;
        U.ClearColor[2] = b;
        U.ClearColor[3] = a;
        m_graph->m_passes[m_pass].Uses.push_back(U);
        return *this;
    }

    PassBuilder& PassBuilder::ColorLoad(TextureHandle h)
    {
        ResourceUse U{};
        U.Handle = h;
        U.Mode = Access::Color;
        U.ClearOnLoad = false;
        m_graph->m_passes[m_pass].Uses.push_back(U);
        return *this;
    }

    PassBuilder& PassBuilder::Depth(TextureHandle h, float ClearDepth)
    {
        ResourceUse U{};
        U.Handle = h;
        U.Mode = Access::Depth;
        U.ClearDepth = ClearDepth;
        m_graph->m_passes[m_pass].Uses.push_back(U);
        return *this;
    }

    void PassBuilder::Execute(std::function<void(CommandList&)> Fn)
    {
        m_graph->m_passes[m_pass].Fn = std::move(Fn);
    }

    // =============================================================================
    // RenderGraph
    // =============================================================================
    RenderGraph::RenderGraph(Device& Dev, CommandList& Cmd) : m_dev(&Dev), m_cmd(&Cmd)
    {
    }

    RenderGraph::~RenderGraph()
    {
        // Auto-execute is intentionally OFF — if you forget Execute() and there
        // are declared passes, this assert catches it before the silent
        // "vkCmdPipelineBarrier2 recorded after vkEndCommandBuffer" validation
        // fountain. Call rg.Execute() (or end the rg's scope) BEFORE
        // RHI.EndFrame(). Transients are still cleaned up below regardless.
        if (!m_executed && !m_passes.empty())
        {
            HELIO_LOG_CRITICAL("Renderer",
                               "RenderGraph destroyed without Execute() being called. "
                               "{} pass(es) were declared but never recorded. "
                               "Call rg.Execute() before RHI.EndFrame(), or scope `rg` to end before EndFrame.",
                               m_passes.size());
            HELIO_CHECK(m_executed);
        }
        for (auto h : m_transients) m_dev->DestroyTexture(h);
    }

    TextureHandle RenderGraph::Image(const char* Name,
                                     uint32_t Width, uint32_t Height,
                                     Format Fmt, TextureUsage Usage)
    {
        auto Tex = m_dev->CreateTexture({
            .Width = Width,
            .Height = Height,
            .Fmt = Fmt,
            .Usage = Usage | TextureUsage::TransferSrc, // TransferSrc lets us Present() blit it
            .DebugName = Name,
        });
        m_transients.push_back(Tex);
        return Tex;
    }

    PassBuilder RenderGraph::Graphics(const char* Name)
    {
        Pass P{};
        P.Name = Name;
        P.Kind = PassKind::Graphics;
        m_passes.push_back(std::move(P));
        return PassBuilder(*this, static_cast<uint32_t>(m_passes.size() - 1));
    }

    PassBuilder RenderGraph::Compute(const char* Name)
    {
        Pass P{};
        P.Name = Name;
        P.Kind = PassKind::Compute;
        m_passes.push_back(std::move(P));
        return PassBuilder(*this, static_cast<uint32_t>(m_passes.size() - 1));
    }

    void RenderGraph::Present(TextureHandle Src)
    {
        m_presentSrc = Src;
    }

    void RenderGraph::Execute()
    {
        if (m_executed) return;
        m_executed = true;

        HELIO_PROFILE_ZONE("RenderGraph::Execute");

        for (const auto& P : m_passes)
        {
            ExecutePass(P);
        }

        if (m_presentSrc.IsValid())
        {
            HELIO_PROFILE_ZONE("RenderGraph::Present");
            // Final output → swapchain. BlitToSwapchain transitions src to
            // TRANSFER_SRC and the swapchain image to TRANSFER_DST, then blits.
            m_cmd->BlitToSwapchain(m_presentSrc);
        }
    }

    void RenderGraph::ExecutePass(const Pass& P)
    {
        // (Per-pass Tracy zone needs compile-time string literals — Phase 13 polish
        //  will hook the dynamic name in via TracyMessage/ZoneText.)

        // 1. Insert layout transitions for Read/Write accesses. Color/Depth are
        //    handled inside BeginRendering's auto-transitions.
        for (const auto& U : P.Uses)
        {
            switch (U.Mode)
            {
            case Access::Read:
                m_cmd->TransitionForSampling(U.Handle);
                break;
            case Access::Write:
                m_cmd->TransitionForStorageWrite(U.Handle);
                break;
            case Access::Color:
            case Access::Depth:
                break;
            }
        }

        // 2. For graphics passes, open the dynamic-rendering scope with declared attachments.
        if (P.Kind == PassKind::Graphics)
        {
            std::vector<rhi::ColorAttachment> Colors;
            rhi::DepthAttachment Depth{};
            bool HasDepth = false;

            for (const auto& U : P.Uses)
            {
                if (U.Mode == Access::Color)
                {
                    rhi::ColorAttachment C{};
                    C.Target = U.Handle;
                    C.Load = U.ClearOnLoad ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
                    C.ClearColor[0] = U.ClearColor[0];
                    C.ClearColor[1] = U.ClearColor[1];
                    C.ClearColor[2] = U.ClearColor[2];
                    C.ClearColor[3] = U.ClearColor[3];
                    Colors.push_back(C);
                }
                else if (U.Mode == Access::Depth)
                {
                    Depth.Target = U.Handle;
                    Depth.Load = U.ClearOnLoad ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
                    Depth.ClearDepth = U.ClearDepth;
                    HasDepth = true;
                }
            }

            // Graphics pass with no color targets is unusual but legal (depth-only
            // shadow map writes). Validation will catch genuinely empty passes.
            m_cmd->BeginRendering(Colors.data(),
                                  static_cast<uint32_t>(Colors.size()),
                                  HasDepth ? &Depth : nullptr);
            if (P.Fn) P.Fn(*m_cmd);
            m_cmd->EndRendering();
        }
        else
        {
            // Compute pass — user dispatches inside the callback.
            if (P.Fn) P.Fn(*m_cmd);
        }
    }
} // namespace helio::render
