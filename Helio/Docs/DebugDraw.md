# Debug Draw

Immediate-mode debug primitives — `Line`, `Box`, `Sphere`, `Text2D`, `Text3D` — for visualizing what your game is thinking about. Call from anywhere; the primitives accumulate per-frame and get drawn on top of your scene.

## API at a glance

```cpp
#include <Renderer/Debug/DebugDraw.h>
```

| Free function (in `helio::debug`) | Effect |
|---|---|
| `Line(p0, p1, RGBA, lifetime=0, "Cat")` | one line segment in world space |
| `Box(min, max, RGBA, lifetime=0, "Cat")` | AABB wireframe (12 edges) |
| `Sphere(center, radius, RGBA, lifetime=0, "Cat")` | 3 great circles, 24 segments each |
| `Text2D(x, y, str, RGBA, lifetime=0, "Cat")` | screen-space text in pixels |
| `Text3D(worldPos, str, RGBA, lifetime=0, "Cat")` | text billboarded at a world point |
| `SetCategoryVisible("Cat", on)` | toggle a whole category at runtime |
| `Clear()` / `ClearCategory("Cat")` | drop accumulated primitives |
| `SetInstance(&dd)` | register the singleton the free fns forward to |

Lifetimes: `0` (default) = "this frame only" — submit every frame from your tick. `> 0` seconds = persist across frames, decay via `Tick(dt)`.

Colors are 0xAABBGGRR. Use the constants `kColorRed`/`kColorGreen`/`kColorBlue`/`kColorYellow`/`kColorMagenta`/`kColorCyan`/`kColorWhite`, or `PackColor(r, g, b, a)`.

Categories are arbitrary strings. The default category is `"Default"`. Common pattern: `"Physics"`, `"AI.Pathfinding"`, `"Audio"` — toggle with a debug menu without recompiling.

## Quick "I want to see it" recipe

```cpp
#include <Core/Logging/Log.h>
#include <Core/Math/Math.h>
#include <Core/Time/Clock.h>
#include <Input/ActionMap.h>
#include <Platform/Windows/Window.h>
#include <Renderer/Debug/DebugDraw.h>
#include <Renderer/Overlay/Overlay.h>
#include <Renderer/RenderGraph.h>
#include <RHI/Public/Device.h>

int main() {
    helio::log::Init();
    helio::platform::windows::Window Win({.Title="DebugDraw demo", .Width=1280, .Height=720});
    helio::rhi::Device RHI({.NativeWindow = Win.Native(),
                            .InitialWidth = Win.Width(), .InitialHeight = Win.Height()});

    helio::render::overlay::Overlay Hud(RHI, helio::rhi::Format::BGRA8_SRGB);
    helio::render::debug::DebugDraw DD(RHI, helio::rhi::Format::BGRA8_SRGB, &Hud);
    helio::debug::SetInstance(&DD);    // free fns now route to DD

    helio::input::ActionMap Map;
    Map.BindKey("Quit",            helio::input::Key::Escape);
    Map.BindKey("Toggle.Physics",  helio::input::Key::P);
    Win.Dispatcher().SetActionMap(&Map);
    Win.Dispatcher().OnActionPressed("Quit", [&]{ Win.RequestClose(); });
    Win.Dispatcher().OnActionPressed("Toggle.Physics", []{
        bool On = helio::debug::IsCategoryVisible("Physics");
        helio::debug::SetCategoryVisible("Physics", !On);
    });

    // Camera (LH, reverse-Z) looking down -Z, ~5m from origin.
    helio::float4x4 View = helio::math::LookAtLH({0, 1.5f, -5}, {0, 0, 0}, {0, 1, 0});
    helio::float4x4 Proj = helio::math::PerspectiveReverseZLH(
        /*FovY=*/1.0f, /*Aspect=*/16.0f/9.0f, /*Near=*/0.1f);
    helio::float4x4 ViewProj = hlslpp::mul(Proj, View);

    helio::core::Clock Clk;
    while (Win.PumpEvents()) {
        const float Dt = float(Clk.Tick());

        // ---- Submit debug primitives every frame -------------------------
        helio::debug::Line({-1,0,0}, {1,0,0}, helio::render::debug::kColorRed);
        helio::debug::Line({0,-1,0}, {0,1,0}, helio::render::debug::kColorGreen);
        helio::debug::Line({0,0,-1}, {0,0,1}, helio::render::debug::kColorBlue);
        helio::debug::Box({-0.5f,-0.5f,-0.5f}, {0.5f,0.5f,0.5f},
                          helio::render::debug::kColorYellow, 0.0f, "Physics");
        helio::debug::Sphere({2, 0, 0}, 0.4f, helio::render::debug::kColorMagenta);
        helio::debug::Text3D({0, 1.2f, 0}, "ORIGIN", helio::render::debug::kColorWhite);
        helio::debug::Text2D(8, 32, "PRESS P TO TOGGLE PHYSICS",
                             helio::render::debug::kColorCyan);

        DD.Tick(Dt);

        if (auto* Cmd = RHI.BeginFrame()) {
            helio::render::RenderGraph Rg(RHI, *Cmd);

            const uint32_t W = uint32_t(Win.Width()), H = uint32_t(Win.Height());
            auto Color = Rg.Image("Color", W, H, helio::rhi::Format::BGRA8_SRGB,
                                  helio::rhi::TextureUsage::Sampled |
                                  helio::rhi::TextureUsage::ColorAttachment |
                                  helio::rhi::TextureUsage::TransferSrc);

            Rg.Graphics("Clear").Color(Color, 0.05f, 0.06f, 0.08f, 1.0f)
              .Execute([](helio::rhi::CommandList&){});

            DD.Render(Rg, Color, ViewProj, W, H);   // lines + forwards Text* to Hud
            Hud.Render(Rg, Color, W, H);            // draws text last so it overlays

            Rg.Present(Color);
            Rg.Execute();
            RHI.EndFrame();
        }
    }
    RHI.WaitIdle();
}
```

You should see:
- RGB axis lines through the origin
- A yellow wireframe cube (the `"Physics"` category — press **P** to hide/show)
- A magenta sphere at (2, 0, 0)
- "ORIGIN" billboarded above the cube
- "PRESS P TO TOGGLE PHYSICS" in cyan, top-left

## Class API vs. free functions

The class API is explicit:
```cpp
helio::render::debug::DebugDraw DD(RHI, ColorFmt, &Overlay);
DD.AddLine(...);
DD.Tick(dt);
DD.Render(rg, target, viewProj, w, h);
```

The free function API forwards to a registered singleton:
```cpp
helio::debug::SetInstance(&DD);  // once at boot

// anywhere in your code:
helio::debug::Line(...);
helio::debug::Sphere(...);
```

If no instance is registered, the free functions are no-ops — safe to leave debug calls in code that runs before the engine boots. Per-thread submission is mutex-protected; calling `helio::debug::Line` from a worker thread is fine.

## How rendering hangs together

Every frame:

1. Game code submits primitives via `helio::debug::*` (or `DD.Add*`).
2. `DD.Tick(dt)` decays lifetimes and evicts expired primitives.
3. `DD.Render(rg, target, viewProj, w, h)`:
   - Flattens visible lines (skipping disabled categories) into a contiguous CPU array
   - Uploads to a host-mapped storage buffer in one shot
   - Schedules a `DebugDraw.Lines` graphics pass on `Target` (LoadOp::Load — preserves your scene)
   - For each Text2D / Text3D primitive, forwards to the overlay font's `DrawText` (Text3D first projects through ViewProj → NDC → pixel; off-screen text is culled cheaply)
4. `Hud.Render(rg, target, w, h)` runs AFTER, drawing all queued glyphs in one pass

**Pass ordering matters:** call `DD.Render` before `Hud.Render`. Otherwise the overlay queue is empty when the overlay pass executes — Text2D/Text3D won't show.

## Categories

Categories are an arbitrary string namespace. Anything you submit with `"Physics"` is hidden/shown together via `SetCategoryVisible("Physics", false)`.

Typical pattern: bind keys to toggle categories from a debug menu.
```cpp
Win.Dispatcher().OnActionPressed("Toggle.Pathfinding", []{
    bool On = helio::debug::IsCategoryVisible("AI.Pathfinding");
    helio::debug::SetCategoryVisible("AI.Pathfinding", !On);
});
```

The default category is `"Default"` — what you get if you omit the trailing string. Categories are created on first use (cheap, ~16 bytes + the string). The set persists across `Clear()`; only `ClearCategory("Foo")` removes primitives without removing the category itself.

## Lifetimes

`Lifetime = 0` (default): one-frame primitive. Evicted on the next `Tick`. This is the most common pattern — re-submit every frame from your game tick.

`Lifetime > 0`: seconds remaining. Decays through `Tick(dt)`. Disappears at `<= 0`. Useful for impact effects, raycast hits, slow-fade debug markers:
```cpp
// Show a 2-second red sphere where the ray hit:
helio::debug::Sphere(HitPoint, 0.1f, helio::render::debug::kColorRed, /*lifetime*/2.0f);
```

`Tick` decays everything regardless of category — disabling a category hides its primitives but they still expire on their normal schedule.

## What's wired up

- Single line-list pipeline drives Line/Box/Sphere via CPU-expanded vertices
- Bindless vertex pull (push the storage-buffer slot; vertex shader loads by `SV_VertexID`)
- HostUpload storage buffer (capacity: 65 536 verts / ~32K lines per frame)
- Text2D / Text3D forward to the [Overlay](Overlay.md) font — same 8x8 glyphs
- Text3D camera projection + cheap frustum culling (off-screen text skipped)
- Mutex-protected per-thread submission (`std::mutex`, not lock-free)
- Per-category visibility toggle
- Singleton + free-function forwarders (no-op if no instance registered)

## What's deferred to Phase 13

- **Arrow / Frustum / Capsule** primitives (same pattern, just more line generation)
- **Lock-free MPSC submission queue** — V1 uses a single `std::mutex`. Fine for most projects; matters if you're hammering submission from many threads
- **Depth-tested lines** — V1 lines render on top of everything (no depth-test). Phase 13 adds an opt-in `DepthTest=true` per primitive
- **Instanced unit-mesh primitives** for Box/Sphere — V1 expands them to lines on the CPU. Instanced shader would save bandwidth at the cost of an extra pipeline; not a bottleneck yet
- **Auto-injection into the render graph** — V1 you call `DD.Render` and `Hud.Render` explicitly; planned API: `rg.RegisterDebugDraw(&DD)`
- **Text style** (size, alignment, background fill) — V1 is bare 8x8 monospace

## Gotchas

- **Sphere segments cost real verts.** Each sphere = 24 segments × 3 great circles × 2 verts = 144 line verts. 1000 spheres = 144 000 verts — past the 65 536 per-frame cap. Drop the segment count for crowded scenes (Phase 13 will let you configure it per-sphere).
- **Text3D needs a valid ViewProj.** If you pass an uninitialized matrix or one with `W <= 0`, the text doesn't appear (cheap culling). Double-check your matrix when text mysteriously stops showing.
- **Format / pipeline binding.** The `DebugDraw` constructor's `TargetFormat` bakes into the line pipeline. If you render onto a target with a different format, validation will scream. Use the same format across DebugDraw + Overlay + your scene texture.
- **No depth test.** Lines render on top of the scene regardless of depth — sometimes desirable (always-visible debug markers), sometimes annoying (lines floating in front of opaque geometry). Phase 13 adds opt-in depth testing.
- **`Tick` after submission, before `Render`.** If you Tick first, freshly-submitted single-frame primitives (lifetime=0) get evicted immediately and you see nothing. Order: submit → Tick → Render.

## See also

- [Overlay.md](Overlay.md) — the font Text2D / Text3D shares
- [RenderGraph.md](RenderGraph.md) — pass declaration / barrier semantics
- [Shaders.md](Shaders.md) — Slang + bindless patterns
