# Overlay

A lightweight always-on overlay that renders 8x8 bitmap text on top of any color attachment. Mainly used for the engine's frame-stats HUD (`FPS 144  CPU 1.23 MS  GPU 0.45 MS  PASSES 5`), but the text API is general — call `DrawText` from your game code for any debug HUD work that doesn't need a full UI library.

## API at a glance

| Type | Lives in | Use for |
|---|---|---|
| `helio::render::overlay::Overlay` | `Renderer/Overlay/Overlay.h` | the class itself: owns font + pipeline |
| `Overlay::DrawText(x, y, str, color)` | — | queue a line of text in target-pixel coords |
| `Overlay::DrawStats(cpuMs, gpuMs, passCount)` | — | frame-stats HUD (one line, or two-lines + graph in advanced mode) |
| `Overlay::DrawRect(x, y, w, h, color)` | — | solid colored quad (HUD bars, graph elements) |
| `Overlay::ToggleAdvanced()` / `SetAdvanced(bool)` | — | flip stats display between compact and advanced |
| `Overlay::Render(rg, target, w, h)` | — | schedule the overlay graphics pass into the render graph |
| `Overlay::Render(cmd, w, h)` | — | direct mode — draw into an open `BeginRendering` scope |
| `Overlay::SetVisible(bool)` / `Toggle()` | — | runtime show/hide |
| `PackColor(r,g,b[,a])`, `kColorWhite/Yellow/Red/Green` | — | 0xAABBGGRR color helpers |

Coverage: ASCII 32–127 (printable). Glyphs are 8×8, no kerning (each char advances 8px right). Capacity: 4096 glyphs per frame.

## Quick "I want to see it" recipe

End-to-end: open a window, render a clear color, slap a frame-stats line on top, toggle the overlay with F3. ~50 lines.

```cpp
#include <Core/Logging/Log.h>
#include <Core/Time/Clock.h>
#include <Input/ActionMap.h>
#include <Platform/Windows/Window.h>
#include <Renderer/Overlay/Overlay.h>
#include <Renderer/RenderGraph.h>
#include <RHI/Public/Device.h>

int main() {
    helio::log::Init();
    helio::platform::windows::Window Win({.Title="Overlay demo", .Width=1280, .Height=720});
    helio::rhi::Device RHI({.NativeWindow = Win.Native(),
                            .InitialWidth = Win.Width(), .InitialHeight = Win.Height()});

    // Overlay built against your target color format. Most games use the
    // swapchain's format (BGRA8_SRGB on Windows).
    helio::render::overlay::Overlay Hud(RHI, helio::rhi::Format::BGRA8_SRGB);

    // F3 hides/shows the whole overlay; F4 flips compact ↔ advanced (frametime
    // graph + 1% / 0.1% lows).
    helio::input::ActionMap Map;
    Map.BindKey("Overlay.Toggle",   helio::input::Key::F3);
    Map.BindKey("Overlay.Advanced", helio::input::Key::F4);
    Map.BindKey("Quit",             helio::input::Key::Escape);
    Win.Dispatcher().SetActionMap(&Map);
    Win.Dispatcher().OnActionPressed("Overlay.Toggle",   [&]{ Hud.Toggle(); });
    Win.Dispatcher().OnActionPressed("Overlay.Advanced", [&]{ Hud.ToggleAdvanced(); });
    Win.Dispatcher().OnActionPressed("Quit",           [&]{ Win.RequestClose(); });

    helio::core::Clock Clk;
    while (Win.PumpEvents()) {
        const double FrameStartSec = Clk.SecondsSinceStart();

        if (auto* Cmd = RHI.BeginFrame()) {
            helio::render::RenderGraph Rg(RHI, *Cmd);

            const uint32_t W = uint32_t(Win.Width()), H = uint32_t(Win.Height());
            auto Color = Rg.Image("Color", W, H, helio::rhi::Format::BGRA8_SRGB,
                                  helio::rhi::TextureUsage::Sampled |
                                  helio::rhi::TextureUsage::ColorAttachment |
                                  helio::rhi::TextureUsage::TransferSrc);

            Rg.Graphics("Clear").Color(Color, 0.10f, 0.15f, 0.20f, 1.0f)
              .Execute([](helio::rhi::CommandList&){ /* clear-only */ });

            // Queue overlay text + schedule its pass.
            const double NowSec = Clk.SecondsSinceStart();
            const double CpuMs = (NowSec - FrameStartSec) * 1000.0;
            Hud.DrawStats(CpuMs, RHI.LastFrameGpuMs(), Rg.Passes());
            Hud.Render(Rg, Color, W, H);

            Rg.Present(Color);
            Rg.Execute();
            RHI.EndFrame();
        }
    }
    RHI.WaitIdle();
}
```

You should see a colored line top-left of the window. F3 hides/shows it. **Press F4 to flip into advanced mode** — adds a second line with `AVG / 1% LOW / 0.1% LOW` and a 240-wide frametime graph beneath (one bar per recent frame, green/yellow/red, plus 60fps and 30fps reference lines).

Color-coded: green when CPU ms < 16.7 (60+ fps), yellow < 33.4 (30+ fps), red beyond.

> **Pass count.** `RenderGraph::Passes()` returns the number of passes declared on the graph so far. The overlay just formats whatever you give it — pass a hand-counted number if you want to include passes outside the graph.

> **GPU ms.** `Device::LastFrameGpuMs()` returns the wall-clock GPU duration of the most recently-retired frame (top-of-pipe → bottom-of-pipe in `vkCmdWriteTimestamp2`). Lags ~2 frames behind the CPU (the timestamps are read back when their frame slot's fence next signals). Returns `0.0` for the first 2 frames before the first cycle's data is ready.

## Direct mode (skip the render graph)

If you've already opened a `BeginRendering` scope in a manual command-list flow, use `Render(cmd, w, h)` instead:

```cpp
Cmd->BeginRendering(/* color attachment with LoadOp::Load */);
// ... your draws ...
Hud.DrawStats(CpuMs, GpuMs, PassCount);
Hud.Render(*Cmd, /*ViewWidthPx=*/W, /*ViewHeightPx=*/H);
Cmd->EndRendering();
```

This doesn't open a new dynamic-rendering scope — it just binds the pipeline + pushes the glyph instances + draws.

## What's wired up

- 8x8 IBM-ROM-style font baked into [BitmapFont.h](../Source/Renderer/Overlay/BitmapFont.h) (~770 bytes, no external file)
- R8_UNORM 128x48 font atlas uploaded once at construction; bindless sampled slot
- Storage-buffer-backed glyph SoA (HostUpload) for per-frame upload (32 B per entry, 4096 entries = 128 KB)
- Single instanced quad pipeline (6 verts × N instances) handling **both** glyphs and solid `DrawRect` quads
- 1-bit alpha test via shader `discard` — crisp opaque glyphs, no blend state needed in V1
- Categorical color helpers + green/yellow/red banding tied to CPU ms
- Rolling 240-sample CPU-ms history → AVG / 1% LOW / 0.1% LOW + frametime graph (advanced mode only)

## What's deferred to Phase 13

- **Auto-injection** into the render graph (currently `Hud.Render(...)` is explicit; planned: `rg.RegisterOverlay(&Hud)` and the graph auto-calls before Present)
- **Per-pass GPU ms breakdown** — V1 exposes the whole-frame number via `Device::LastFrameGpuMs()`; per-pass timing lives in Tracy and lands in the public API behind `HELIO_GPU_ZONE` polish
- **Real alpha blending** for fading the overlay (V1 uses shader discard — no smooth fade)
- **Kerning + variable-width font** (V1 is monospace 8x8)
- **Auto target-size discovery via `PassContext`** (V1 requires explicit `(W, H)` on `Render`)
- **Markdown formatting** in DrawText (color spans, bold/italic) — possible but not on the V1 path

## Gotchas

- **Format mismatch is silent until pipeline create.** The `TargetFormat` passed to the `Overlay` constructor bakes into the pipeline. If you later pass a `Target` to `Render(rg, target, ...)` whose format differs, the validation layer screams. Use the same `Format` your final color texture uses.
- **`ColorLoad` preserves prior content.** The overlay pass uses `LoadOp::Load` on the target, so whatever your previous passes drew shows through where the overlay isn't drawing. Don't pass `Target` before you've written into it (you'll see uninitialized contents under the text).
- **Direct mode requires an open `BeginRendering`.** Calling `Render(cmd, w, h)` outside a rendering scope will fail validation. The `Render(rg, ...)` overload opens its own scope automatically.
- **DrawText returns the X cursor.** Chain multiple `DrawText` calls on the same line by using the previous return value: `X = Hud.DrawText(X, Y, "left  "); Hud.DrawText(X, Y, "right", kColorYellow);`.
- **Glyph budget is 4096.** Overflow logs a warning and drops the rest — plenty for HUD use, but if you're rendering walls of debug text, switch to the debug-draw `Text2D` (which shares the same font + budget but uses categories for cheap filtering).

## See also

- [DebugDraw.md](DebugDraw.md) — uses the same font for `Text2D` / `Text3D`
- [Profiling.md](Profiling.md) — connects the on-screen numbers to Tracy zones
