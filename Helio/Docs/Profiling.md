# Profiling

Helio integrates **Tracy** for frame-level CPU + GPU profiling. The Tracy client is linked into `Game.exe`; you connect to it from the standalone `Tracy.exe` server while Game is running.

## What you get

- **CPU zones** — any code wrapped in `HELIO_PROFILE_ZONE("Name")` shows up on Tracy's CPU timeline.
- **Frame markers** — `HELIO_PROFILE_FRAME()` at end-of-frame draws frame boundaries on the timeline.
- **GPU timeline** — Vulkan command-buffer execution time per submitted batch, aligned to CPU frame boundaries. Auto-populated via `TracyVkContext` + `TracyVkCollect` (no per-call code needed in V1).
- **Lock contention, memory tracking, custom plots** — all standard Tracy features work; just use the underlying Tracy macros when you need them.

## API

```cpp
#include <Core/Profile/Profile.h>

void MyFunc() {
    HELIO_PROFILE_ZONE("MyFunc");   // CPU zone scoped to the function
    DoExpensiveThing();
}

void GameFrame() {
    HELIO_PROFILE_FRAME();          // end-of-frame marker
    HELIO_PROFILE_ZONE("Frame");
    // ...
}

// Numeric plot — Tracy graphs the value over time:
HELIO_PROFILE_PLOT("Visible objects", visible.size());
```

Macros, scoped:

| Macro | Type |
|---|---|
| `HELIO_PROFILE_ZONE(Name)` | CPU zone, scoped to enclosing `{}` block |
| `HELIO_PROFILE_FRAME()` | Frame marker (call once per frame on main thread) |
| `HELIO_PROFILE_PLOT(Name, Value)` | Numeric plot (good for "objects culled", "draw calls", "memory MB") |
| `HELIO_PROFILE_GPU(Cmd, Name)` | **Stub in V1** — Phase 13 makes this a real per-pass GPU zone macro |

All macros compile to nothing when `HELIO_TRACY=OFF` (CMake option). Default is ON for Debug + RelWithDebInfo, OFF for Release.

## Getting Tracy.exe

Tracy ships as a separate viewer/server binary. Three ways:

1. **Download a release** — <https://github.com/wolfpld/tracy/releases> — get `Tracy-x.y.z.7z` for Windows, extract, run `tracy-profiler.exe` (or `Tracy.exe` in older releases).
2. **Build from source** — `git clone https://github.com/wolfpld/tracy`, open the VS solution under `profiler/build/win32/Tracy.sln`.
3. **vcpkg** — `vcpkg install tracy[gui]` builds it but is slower than just grabbing the release.

The version of the server should match the Tracy client linked into `Game.exe`. vcpkg's current `tracy` port pins a known version — to check which, grep `vcpkg_installed/x64-windows/include/tracy/common/TracyVersion.hpp`.

## Quick "I want to see it" recipe

1. Build + launch Game:
   ```powershell
   cmake --build build/windows-msvc-debug --target Game --config Debug
   ./build/windows-msvc-debug/bin/Debug/Game.exe
   ```
2. Launch `Tracy.exe` (the standalone viewer).
3. In Tracy's connection dialog: select **localhost** (or `127.0.0.1`) → click **Connect**. Tracy finds the running Game.exe via its embedded TCP listener.
4. You'll see:
   - The **CPU timeline** with a `Startup` zone at program start (from `HELIO_PROFILE_ZONE("Startup")` in main.cpp), plus per-frame `Frame` zones and `Window::PumpEvents` zones tagged from the platform layer.
   - The **GPU timeline** with per-frame VkQueueSubmit2 spans (populated by the `TracyVkCollect` we call at the end of every frame in `VulkanContext::EndFrame`).
   - Frame markers separating each rendered frame.
5. Click any zone for a callstack, source location, child timings, and statistics across all instances.

## What does and doesn't have GPU zones

**Today:** the Tracy GPU timeline shows the wall-clock span of each `vkQueueSubmit2` — useful for "is the GPU idle? saturated? matching CPU frame rate?" but not for "which pass took how long".

A single whole-frame GPU number is also available outside Tracy via **`Device::LastFrameGpuMs()`** — backed by `vkCmdWriteTimestamp2` at top-of-pipe / bottom-of-pipe per frame, read back ~2 frames later. The overlay's `DrawStats(...)` consumes it.

**Phase 13:** per-pass GPU zones via `HELIO_GPU_ZONE(*Cmd, "Triangle")` macros wrapping `vkCmdBeginRendering`/`vkCmdDispatch` blocks. Will show up alongside CPU zones with the same name so you can visually align "this draw kicked off this submit".

## On-screen frame-stats overlay

The bitmap-text overlay ([Overlay.md](Overlay.md)) provides a no-config alternative to Tracy for ad-hoc perf inspection:

- **Compact mode (default)**: one line — `FPS / CPU ms / GPU ms / passes`.
- **Advanced mode** (toggle via `Overlay::ToggleAdvanced()`): adds a second line with `AVG ms / 1% LOW FPS / 0.1% LOW FPS` computed over the last 240 frames, plus a 240px-wide frametime graph with 60fps + 30fps reference lines, color-coded green/yellow/red per frame.

Wire it up with `Hud.DrawStats(CpuMs, RHI.LastFrameGpuMs(), rg.Passes())` once per frame.

## Disabling Tracy

Add `-DHELIO_TRACY=OFF` to your CMake configure:

```powershell
cmake --preset windows-msvc-debug -DHELIO_TRACY=OFF
```

`HELIO_TRACY_ENABLED` flips to 0, every profile macro expands to `((void)0)`, the `TracyClient` link goes away, and `TracyVkContext`/`TracyVkCollect` calls in `VulkanContext` no-op out via the `#if HELIO_TRACY_ENABLED` guards.

Use this for:
- Shipping builds (avoid the client overhead + the listening TCP socket)
- Bisecting "is this overhead from Tracy itself?"

## Reading the on-screen overlay (when Phase 11 lands)

The Phase 11 overlay will show `CPU x.xx ms · GPU x.xx ms · Passes N` in the top-left corner of the swapchain — same data as Tracy, but in-game when you don't want to alt-tab. Currently not implemented; lands as a small bitmap-font pass before the present.

## What's deferred to Phase 13

- **Per-pass GPU zones** (`HELIO_GPU_ZONE`) — see above.
- **Lock contention tracking** — Tracy supports it via `LockableBase<>` wrappers; not exposed in `Profile.h` yet.
- **Memory tracking** — Tracy can hook allocators. VMA allocation tracking via `TracyAllocN`/`TracyFreeN` is straightforward but unimplemented.
- **Network-only Tracy** — currently you connect over localhost. For remote profiling you'd configure `TRACY_NETWORK_PORT` and listen on all interfaces. Not security-reviewed.
