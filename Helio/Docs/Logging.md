# Logging

Helio uses **spdlog** behind a thin facade. Logs go to two sinks simultaneously: a colored console (stdout) and a rotating file at `game/Saved/Logs/Game.log`.

## API

```cpp
#include <Core/Logging/Log.h>

helio::log::Init();                                   // call once at startup
HELIO_LOG_INFO("Game", "Hello {}, {} times", "world", 3);
helio::log::Shutdown();                               // optional but recommended
```

Macros, in increasing severity:

| Macro | Use for |
|---|---|
| `HELIO_LOG_TRACE(cat, ...)` | Per-frame spam, fine-grained tracing |
| `HELIO_LOG_DEBUG(cat, ...)` | Developer-only info, off in release |
| `HELIO_LOG_INFO(cat, ...)` | One-time state changes, lifecycle events |
| `HELIO_LOG_WARN(cat, ...)` | Recoverable issues |
| `HELIO_LOG_ERROR(cat, ...)` | Failed operation, app continues |
| `HELIO_LOG_CRITICAL(cat, ...)` | Unrecoverable, about to assert/crash |

The first arg is a **category** string. Categories cluster related messages and prefix every line in console + file output. Common categories already in use:

| Category | Owner |
|---|---|
| `Engine` | High-level lifecycle (init/shutdown) |
| `RHI` | Vulkan device, swapchain, resource creation |
| `Vulkan` | Validation layer messages (routed automatically) |
| `Renderer` | Render graph / overlay / debug-draw |
| `Input` | Key/mouse/window events |
| `Platform` | Window, SDL events, file watcher |
| `Assert` | `HELIO_CHECK` / `HELIO_ASSERT` failures |
| `Game` | Game-side code (your default) |

Categories are just strings — invent new ones freely. They're lazily created and cached.

## Format string

Uses `fmt`/`std::format` syntax: `{}`, `{0}`, `{:.3f}`, `{:#x}`, etc. Type-safe and no `printf`-style format-string vulnerabilities.

```cpp
HELIO_LOG_INFO("Game", "Frame {} took {:.3f}ms ({:.1f} FPS)",
               FrameIndex, FrameMs, 1000.0 / FrameMs);
```

## Vulkan validation routing

The Vulkan debug-utils messenger callback inside `VulkanContext::DebugCallback` translates severity levels to log levels and emits under the `Vulkan` category:

| Vulkan severity | spdlog level |
|---|---|
| `ERROR_BIT_EXT` | `error` |
| `WARNING_BIT_EXT` | `warn` |
| `INFO_BIT_EXT` | `info` |
| (anything else) | `debug` |

So validation issues appear in your normal log stream — no extra setup needed.

## File output

- **Path:** `Saved/Logs/Game.log` resolved **relative to the current working directory** when `Game.exe` runs. CMake sets `VS_DEBUGGER_WORKING_DIRECTORY = $<TARGET_FILE_DIR:Game>` so debugger launches put the log next to the binary (`build/.../bin/Debug/Saved/Logs/Game.log`). If you run from PowerShell with `./Game.exe`, the file goes next to the binary too because that's the cwd.
- **Format:** `[YYYY-MM-DD HH:MM:SS.mmm] [Category] [Level] message`
- **Rotation:** none today. The file grows unbounded. Phase 13 polish adds a rotating sink.
- **Flushing:** auto-flushes on `warn` or higher. `trace`/`debug`/`info` may sit in buffer; call `helio::log::Shutdown()` (or just let the program exit normally) to flush.

## Quick "I want to see it" recipe

```cpp
#include <Core/Logging/Log.h>

int main() {
    helio::log::Init();

    HELIO_LOG_INFO("Game", "Started");
    HELIO_LOG_WARN("Game", "Threshold {} is high", 0.95f);
    HELIO_LOG_ERROR("Game", "Failed to load '{}'", "missing.dat");

    helio::log::Shutdown();
}
```

Run it, then:

```powershell
# Console will show:
# [13:42:01.234] [Game] [info] Started
# [13:42:01.234] [Game] [warning] Threshold 0.95 is high
# [13:42:01.234] [Game] [error] Failed to load 'missing.dat'

# Same lines also written to:
cat build/windows-msvc-debug/bin/Debug/Saved/Logs/Game.log
```

## Common gotchas

- **No `Init()` call** — log macros invoked before `helio::log::Init()` will create a logger with no sinks attached. Lines silently vanish. Always init first.
- **File sink fails silently** — if the `Saved/Logs/` directory can't be created (permissions, full disk), the engine warns once to the console sink and continues with console-only. Check console output if `Game.log` is empty.
- **Format mismatch crashes** — `HELIO_LOG_INFO("cat", "value = {}", )` (missing arg) is a compile-time error thanks to `fmt`'s constexpr parsing. But `"{} {}"` with one arg slips through and throws at runtime.

## What's deferred to Phase 13

- **Async logging** — current implementation is sync; high-frequency `TRACE` calls block the caller. Phase 13 swaps in spdlog's async logger.
- **Rotating file sink** — `Game.log` currently grows unbounded.
- **Per-category level filters** — today every category logs at every level. Filtering happens at the sink. Phase 13 adds runtime `SetCategoryLevel("Vulkan", warn)` to suppress noisy categories.
