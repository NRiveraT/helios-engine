/// @file Overlay.h
/// @brief Lightweight always-on engine overlay: bitmap text + frame stats.
///
/// Owns a baked 8x8 font texture and an instanced quad pipeline. Game code
/// queues text per frame, then either calls `Render(rg, target)` to add a
/// graphics pass to the render graph, or calls `Render(cmd, target)` to draw
/// straight into a command list inside an existing pass.
///
/// Stats convenience: `DrawStats(cpuMs, gpuMs, passCount)` formats
/// `"CPU x.xx ms  GPU x.xx ms  PASSES N"` and queues it top-left.
///
/// Toggle pattern: wire `Visible(bool)` to a `Dispatcher::OnActionPressed`
/// handler (default action name in the docs is `"Overlay.Toggle"`, F3).
///
/// Capacity: 4096 glyphs per frame (8x8 means ~32K screen-pixel coverage —
/// vastly more than the stats line needs). Excess `DrawText` calls drop.
#pragma once

#include <RHI/Public/Buffer.h>
#include <RHI/Public/Pipeline.h>
#include <RHI/Public/RingUploadBuffer.h>
#include <RHI/Public/Texture.h>

#include <memory>

#include <cstdint>
#include <string_view>

namespace helio::rhi { class Device; class CommandList; }
namespace helio::render { class RenderGraph; }

namespace helio::render::overlay {

inline constexpr uint32_t kMaxGlyphsPerFrame = 4096;

/// 0xAABBGGRR packed color. Top-left visibility: 0xFFFFFFFF = white opaque.
inline constexpr uint32_t PackColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255) {
    return  (uint32_t(A) << 24)
          | (uint32_t(B) << 16)
          | (uint32_t(G) <<  8)
          |  uint32_t(R);
}

inline constexpr uint32_t kColorWhite  = PackColor(255, 255, 255);
inline constexpr uint32_t kColorYellow = PackColor(255, 220,  64);
inline constexpr uint32_t kColorRed    = PackColor(255,  80,  80);
inline constexpr uint32_t kColorGreen  = PackColor( 96, 255, 128);

class Overlay {
public:
    /// Build the font atlas + storage buffer + pipeline. Pipeline is created
    /// against `TargetFormat` (must match the texture you'll later pass to
    /// `Render`); if the target format changes mid-run you must recreate the
    /// overlay. Most games render the overlay onto the same color texture
    /// they Present, so this is set once at boot.
    Overlay(rhi::Device& Dev, rhi::Format TargetFormat);
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // ---- Per-frame submission -------------------------------------------

    /// Drop all queued text. Call at frame start (or implicitly via `Render`).
    void Clear() noexcept;

    /// Queue `Text` for the next `Render`. `(X, Y)` are in pixels relative
    /// to the target texture top-left (0..Width-1, 0..Height-1). Glyphs are
    /// 8x8 with no kerning — advance is always 8px right per character.
    /// Returns the X position immediately after the last glyph.
    uint32_t DrawText(int X, int Y, std::string_view Text, uint32_t RGBAColor = kColorWhite);

    /// Queue a solid colored rectangle. Same pipeline as text — uses the
    /// reserved `Code = 0xFFFFFFFF` glyph slot which the shader interprets
    /// as "skip font sample, output solid color". Useful for the frametime
    /// graph background / bars in advanced mode, or any in-game HUD bar.
    void DrawRect(int X, int Y, int Width, int Height, uint32_t RGBAColor);

    /// Convenience: format frame stats and queue them top-left.
    ///
    /// In **compact** mode (default): one line — `FPS / CPU / GPU / PASSES`.
    /// In **advanced** mode: two lines + a frametime graph below them, with
    /// 1% and 0.1% lows computed over the last ~240 frames. Toggle with
    /// `SetAdvanced` / `ToggleAdvanced` (typically bind to F4).
    ///
    /// FPS is the instantaneous `1000 / CpuMs` — expect frame-to-frame jitter.
    /// Color-codes the top line by whichever of CPU / GPU ms is higher.
    void DrawStats(double CpuMs, double GpuMs, uint32_t PassCount);

    /// Switch between the one-line compact stats and the two-line advanced
    /// view that adds 1% / 0.1% lows and a frametime graph.
    void SetAdvanced(bool On) noexcept { m_advanced = On; }
    void ToggleAdvanced() noexcept { m_advanced = !m_advanced; }
    [[nodiscard]] bool IsAdvanced() const noexcept { return m_advanced; }

    // ---- Render ---------------------------------------------------------

    /// Schedule an overlay graphics pass onto `Target`. Adds a `LoadOp::Load`
    /// pass that draws all queued glyphs and clears the queue on submit.
    /// `Target` must have been created with `ColorAttachment` usage and its
    /// format must match the `TargetFormat` passed to the constructor.
    /// Pass the target's pixel dimensions — the shader needs them to convert
    /// glyph positions to NDC (Phase 13's PassContext will remove this).
    ///
    /// Call after all your other graph passes that write into `Target`, and
    /// before `rg.Present(Target)`.
    void Render(RenderGraph& Rg, rhi::TextureHandle Target,
                uint32_t TargetWidthPx, uint32_t TargetHeightPx);

    /// Direct mode: draw into the current rendering scope of `Cmd`. Use when
    /// the overlay is the last thing in an existing `BeginRendering` pass.
    /// `(ViewWidthPx, ViewHeightPx)` are the framebuffer dimensions.
    void Render(rhi::CommandList& Cmd, uint32_t ViewWidthPx, uint32_t ViewHeightPx);

    // ---- Visibility -----------------------------------------------------

    [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
    void SetVisible(bool On) noexcept { m_visible = On; }
    void Toggle() noexcept { m_visible = !m_visible; }

private:
    void UploadAndDraw(rhi::CommandList& Cmd, uint32_t ViewWidthPx, uint32_t ViewHeightPx);

    rhi::Device*        m_dev{nullptr};
    rhi::Format         m_targetFormat{};

    rhi::TextureHandle  m_fontTex{};
    /// Per-frame-slot ring buffer for the glyph SoA. Replaces the previous
    /// single host-upload buffer that had a cross-frame race.
    std::unique_ptr<rhi::RingUploadBuffer> m_glyphRing;
    rhi::PipelineHandle m_pipeline{};

    // CPU-side glyph queue (uploaded to m_glyphBuf each Render).
    // `Code = 0xFFFFFFFF` means "this is a solid rect, ignore the font sample
    // and output `RGBA` directly". `(SizeX, SizeY)` is the quad's pixel
    // dimensions — 8x8 for glyphs, arbitrary for rects.
    struct Glyph {
        float    PixelPosX;
        float    PixelPosY;
        float    SizeX;
        float    SizeY;
        uint32_t Code;
        uint32_t RGBA;
        uint32_t _Pad0;
        uint32_t _Pad1;
    };
    static_assert(sizeof(Glyph) == 32, "Glyph layout must match Overlay.slang");

    static constexpr uint32_t kSolidRectCode = 0xFFFFFFFFu;

    void DrawStatsAdvanced(double CpuMs, double GpuMs, uint32_t PassCount);

    Glyph    m_glyphs[kMaxGlyphsPerFrame]{};
    uint32_t m_glyphCount{0};
    bool     m_visible{true};
    bool     m_advanced{true};

    // Rolling CPU-ms history for percentile lows + frametime graph.
    static constexpr uint32_t kFrametimeHistory = 240;
    double   m_frametimeHistoryMs[kFrametimeHistory]{};
    uint32_t m_frametimeHead{0};
    uint32_t m_frametimeCount{0};
};

} // namespace helio::render::overlay
