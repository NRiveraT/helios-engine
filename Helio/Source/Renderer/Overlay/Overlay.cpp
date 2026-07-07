#include "Overlay.h"
#include "BitmapFont.h"

#include <Renderer/RenderGraph.h>

#include <RHI/Public/Buffer.h>
#include <RHI/Public/CommandList.h>
#include <RHI/Public/Device.h>
#include <RHI/Public/Pipeline.h>
#include <RHI/Public/Texture.h>

#include <Core/Assert/Assert.h>
#include <Core/Logging/Log.h>
#include <Core/Profile/Profile.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace helio::render::overlay
{
    namespace
    {
        // Glyph SoA pushed to the GPU.
        struct PushConsts
        {
            uint32_t FontTextureSlot;
            uint32_t GlyphBufferSlot;
            float ViewWidthPx;
            float ViewHeightPx;
        };

        static_assert(sizeof(PushConsts) == 16, "PushConsts must match Overlay.slang");
    } // namespace

    Overlay::Overlay(rhi::Device& Dev, rhi::Format TargetFormat)
        : m_dev(&Dev), m_targetFormat(TargetFormat)
    {
        // ---- Build the font atlas texture --------------------------------------
        uint8_t Pixels[kFontAtlasWidthPx * kFontAtlasHeightPx]{};
        ExpandFontToAtlas(Pixels);

        m_fontTex = m_dev->CreateTexture({
            .Width = kFontAtlasWidthPx,
            .Height = kFontAtlasHeightPx,
            .Fmt = rhi::Format::R8_UNORM,
            .Usage = rhi::TextureUsage::Sampled,
            .DebugName = "Overlay.FontAtlas",
            .InitialData = Pixels,
            .InitialDataSize = sizeof(Pixels),
        });
        HELIO_CHECK(m_fontTex.IsValid());
        HELIO_CHECK(m_fontTex.SampledSlot != UINT32_MAX);

        // ---- Build the per-frame glyph ring (one host-upload buffer per slot) --
        m_glyphRing = std::make_unique<rhi::RingUploadBuffer>(
            Dev, kMaxGlyphsPerFrame * sizeof(Glyph), "Overlay.GlyphBuffer");

        // ---- Pipeline ----------------------------------------------------------
        rhi::GraphicsPipelineDesc Pd{};
        Pd.ShaderPath = "Shaders/Overlay/Overlay.spv";
        Pd.VertexEntry = "VSMain";
        Pd.FragmentEntry = "PSMain";
        Pd.ColorFormats[0] = m_targetFormat;
        Pd.ColorAttachmentCount = 1;
        Pd.DepthFormat = rhi::Format::Undefined;
        Pd.Topology = rhi::PrimitiveTopology::TriangleList;
        Pd.Cull = rhi::CullMode::None;
        Pd.DepthTest = false;
        Pd.DepthWrite = false;
        Pd.PushConstantBytes = sizeof(PushConsts);
        Pd.DebugName = "Overlay";
        m_pipeline = m_dev->CreateGraphicsPipeline(Pd);
        HELIO_CHECK(m_pipeline.IsValid());

        HELIO_LOG_INFO("Renderer", "Overlay initialized (font {}x{}, {} glyphs)",
                       kFontAtlasWidthPx, kFontAtlasHeightPx, kFontGlyphCount);
    }

    Overlay::~Overlay()
    {
        if (m_dev)
        {
            if (m_pipeline.IsValid()) m_dev->DestroyPipeline(m_pipeline);
            m_glyphRing.reset(); // RingUploadBuffer dtor frees its slots
            if (m_fontTex.IsValid()) m_dev->DestroyTexture(m_fontTex);
        }
    }

    void Overlay::Clear() noexcept
    {
        m_glyphCount = 0;
    }

    uint32_t Overlay::DrawText(int X, int Y, std::string_view Text, uint32_t RGBAColor)
    {
        if (!m_visible) return static_cast<uint32_t>(X);
        int CursorX = X;
        for (char C : Text)
        {
            if (m_glyphCount >= kMaxGlyphsPerFrame)
            {
                HELIO_LOG_WARN("Renderer", "Overlay glyph buffer full ({} glyphs), dropping rest",
                               kMaxGlyphsPerFrame);
                break;
            }
            Glyph& G = m_glyphs[m_glyphCount++];
            G.PixelPosX = static_cast<float>(CursorX);
            G.PixelPosY = static_cast<float>(Y);
            G.SizeX = static_cast<float>(kFontGlyphWidthPx);
            G.SizeY = static_cast<float>(kFontGlyphHeightPx);
            G.Code = static_cast<uint8_t>(C);
            G.RGBA = RGBAColor;
            CursorX += static_cast<int>(kFontGlyphWidthPx);
        }
        return static_cast<uint32_t>(CursorX);
    }

    void Overlay::DrawRect(int X, int Y, int Width, int Height, uint32_t RGBAColor)
    {
        if (!m_visible || Width <= 0 || Height <= 0) return;
        if (m_glyphCount >= kMaxGlyphsPerFrame)
        {
            HELIO_LOG_WARN("Renderer", "Overlay glyph buffer full ({} entries), dropping rect",
                           kMaxGlyphsPerFrame);
            return;
        }
        Glyph& G = m_glyphs[m_glyphCount++];
        G.PixelPosX = static_cast<float>(X);
        G.PixelPosY = static_cast<float>(Y);
        G.SizeX = static_cast<float>(Width);
        G.SizeY = static_cast<float>(Height);
        G.Code = kSolidRectCode;
        G.RGBA = RGBAColor;
    }

    namespace
    {
        inline uint32_t PickStatsColor(double Ms)
        {
            if (Ms < 16.7) return kColorGreen;
            if (Ms < 33.4) return kColorYellow;
            return kColorRed;
        }
    }

    void Overlay::DrawStats(double CpuMs, double GpuMs, uint32_t PassCount)
    {
        // Always push the sample to history so the rolling window keeps moving
        // even while the overlay is hidden or compact. Cheap (~constant time).
        m_frametimeHistoryMs[m_frametimeHead] = CpuMs > 0.0 ? CpuMs : 0.0001;
        m_frametimeHead = (m_frametimeHead + 1) % kFrametimeHistory;
        if (m_frametimeCount < kFrametimeHistory) ++m_frametimeCount;

        if (!m_visible) return;

        if (m_advanced)
        {
            DrawStatsAdvanced(CpuMs, GpuMs, PassCount);
            return;
        }

        const uint32_t Fps = CpuMs > 0.0001 ? uint32_t(1000.0 / CpuMs + 0.5) : 0;
        char Line[96]{};
        std::snprintf(Line, sizeof(Line),
                      "FPS %3u  CPU %5.2f MS  GPU %5.2f MS  PASSES %u",
                      Fps, CpuMs, GpuMs, PassCount);
        DrawText(/*X=*/8, /*Y=*/8, Line, PickStatsColor(CpuMs > GpuMs ? CpuMs : GpuMs));
    }

    void Overlay::DrawStatsAdvanced(double CpuMs, double GpuMs, uint32_t PassCount)
    {
        // ---- Line 1: instantaneous numbers -----------------------------------
        const uint32_t Fps = CpuMs > 0.0001 ? uint32_t(1000.0 / CpuMs + 0.5) : 0;
        char L1[96]{};
        std::snprintf(L1, sizeof(L1),
                      "FPS %3u  CPU %5.2f MS  GPU %5.2f MS  PASSES %u",
                      Fps, CpuMs, GpuMs, PassCount);
        DrawText(8, 8, L1, PickStatsColor(CpuMs > GpuMs ? CpuMs : GpuMs));

        // ---- Compute percentiles over the rolling window ---------------------
        double Sorted[kFrametimeHistory];
        const uint32_t N = m_frametimeCount;
        for (uint32_t I = 0; I < N; ++I) Sorted[I] = m_frametimeHistoryMs[I];
        std::sort(Sorted, Sorted + N);

        double Sum = 0.0;
        for (uint32_t I = 0; I < N; ++I) Sum += Sorted[I];
        const double AvgMs = N > 0 ? Sum / double(N) : 0.0;

        // 1% low FPS = the FPS at the 99th-percentile FRAMETIME (= slow frame).
        // For N=240 that's index 237 (the 3rd-worst). 0.1% picks the worst frame.
        const uint32_t IdxP99 = N > 0 ? std::min<uint32_t>(N - 1, uint32_t(double(N) * 0.99)) : 0;
        const uint32_t IdxP999 = N > 0 ? std::min<uint32_t>(N - 1, uint32_t(double(N) * 0.999)) : 0;
        const double Low1Ms = N > 0 ? Sorted[IdxP99] : 0.0;
        const double Low01Ms = N > 0 ? Sorted[IdxP999] : 0.0;
        const uint32_t Low1Fps = Low1Ms > 0.0001 ? uint32_t(1000.0 / Low1Ms + 0.5) : 0;
        const uint32_t Low01Fps = Low01Ms > 0.0001 ? uint32_t(1000.0 / Low01Ms + 0.5) : 0;

        char L2[96]{};
        std::snprintf(L2, sizeof(L2),
                      "AVG %5.2f MS  1%% LOW %3u FPS  0.1%% LOW %3u FPS",
                      AvgMs, Low1Fps, Low01Fps);
        DrawText(8, 20, L2, kColorWhite);

        // ---- Frametime graph -------------------------------------------------
        if (N == 0) return;

        constexpr int GraphX = 8;
        constexpr int GraphY = 36;
        constexpr int GraphW = int(kFrametimeHistory); // 240px wide
        constexpr int GraphH = 48; // 48px tall
        constexpr double GraphMaxMs = 50.0; // cap bars at 50ms (20fps)

        // Background panel (semi-opaque dark — opacity placeholder until blend).
        DrawRect(GraphX, GraphY, GraphW, GraphH, PackColor(12, 14, 20, 255));

        // 60fps reference line (16.67 ms).
        const int Sixty = GraphY + GraphH - int(16.67 / GraphMaxMs * double(GraphH));
        DrawRect(GraphX, Sixty, GraphW, 1, PackColor(80, 200, 120, 255));

        // 30fps reference line (33.33 ms).
        const int Thirty = GraphY + GraphH - int(33.33 / GraphMaxMs * double(GraphH));
        DrawRect(GraphX, Thirty, GraphW, 1, PackColor(220, 200, 80, 255));

        // One 1-pixel-wide column per historical sample (oldest left, newest right).
        // Iterate in chronological order: oldest = (head + capacity - count) % capacity.
        const uint32_t StartIdx = (m_frametimeHead + kFrametimeHistory - N) % kFrametimeHistory;
        for (uint32_t I = 0; I < N; ++I)
        {
            const uint32_t Slot = (StartIdx + I) % kFrametimeHistory;
            const double Ms = m_frametimeHistoryMs[Slot];
            int BarH = int(Ms / GraphMaxMs * double(GraphH) + 0.5);
            if (BarH < 1) BarH = 1;
            if (BarH > GraphH) BarH = GraphH;
            const int BarX = GraphX + int(I);
            const int BarY = GraphY + GraphH - BarH;
            DrawRect(BarX, BarY, 1, BarH, PickStatsColor(Ms));
        }
    }

    void Overlay::Render(RenderGraph& Rg, rhi::TextureHandle Target,
                         uint32_t TargetWidthPx, uint32_t TargetHeightPx)
    {
        if (!m_visible || m_glyphCount == 0)
        {
            Clear();
            return;
        }

        Rg.Graphics("Overlay")
          .ColorLoad(Target)
          .Execute([this, TargetWidthPx, TargetHeightPx](rhi::CommandList& Cmd)
          {
              UploadAndDraw(Cmd, TargetWidthPx, TargetHeightPx);
          });
    }

    void Overlay::Render(rhi::CommandList& Cmd, uint32_t ViewWidthPx, uint32_t ViewHeightPx)
    {
        if (!m_visible || m_glyphCount == 0)
        {
            Clear();
            return;
        }
        UploadAndDraw(Cmd, ViewWidthPx, ViewHeightPx);
    }

    void Overlay::UploadAndDraw(rhi::CommandList& Cmd, uint32_t ViewWidthPx, uint32_t ViewHeightPx)
    {
        HELIO_PROFILE_ZONE("Overlay::Render");

        // Upload glyph data into THIS frame's slot. Avoids the race that a
        // single shared host-upload buffer would have with FramesInFlight>1.
        m_glyphRing->Write(/*Offset=*/0, m_glyphs, m_glyphCount * sizeof(Glyph));
        const rhi::BufferHandle CurSlot = m_glyphRing->Current();

        PushConsts Pc{};
        Pc.FontTextureSlot = m_fontTex.SampledSlot;
        Pc.GlyphBufferSlot = CurSlot.BindlessSlot;
        // If the caller didn't pass an extent, SetViewportFull below uses the
        // swapchain extent. Pass dummy 1x1 — the shader divides by these to get
        // NDC; if they're wrong, text positions are wrong but won't crash.
        // The Render(rg, target) overload doesn't yet know the target extent
        // (Phase 13 PassContext fix); recommend using Render(cmd, w, h) when
        // the target isn't swapchain-sized.
        Pc.ViewWidthPx = ViewWidthPx ? static_cast<float>(ViewWidthPx) : 1.0f;
        Pc.ViewHeightPx = ViewHeightPx ? static_cast<float>(ViewHeightPx) : 1.0f;

        Cmd.Bind(m_pipeline);
        Cmd.SetViewportFull();
        Cmd.Push(Pc);
        Cmd.Draw(/*VertexCount=*/6, /*InstanceCount=*/m_glyphCount);

        // Frame complete — clear the queue for next frame.
        Clear();
    }
} // namespace helio::render::overlay
