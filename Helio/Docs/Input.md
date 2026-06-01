# Input

Platform-agnostic input handling: `Key` / `MouseButton` / `KeyState` enums + tagged-union `InputEvent` + `ActionMap` for symbolic bindings + `Dispatcher` for subscriptions. SDL is hidden inside the platform layer — game code never sees an `SDLK_*` constant.

## API at a glance

| Type | Lives in | Use for |
|---|---|---|
| `Key`, `MouseButton`, `KeyState` | `Input/Keys.h` | abstract keycodes |
| `InputEvent` (tagged union) | `Input/Event.h` | one event from any source (key, mouse, window) |
| `ActionMap` | `Input/ActionMap.h` | bind keys/buttons to action names ("Quit", "Move.Forward") |
| `Dispatcher` | `Input/Dispatcher.h` | subscribe handlers to actions or raw events |
| `Window::Dispatcher()` | `Platform/Windows/Window.h` | the dispatcher the platform pumps events into |

## Two ways to listen for input

### 1. Action-based (recommended for game logic)

```cpp
#include <Input/ActionMap.h>
#include <Input/Dispatcher.h>
#include <Platform/Windows/Window.h>

helio::input::ActionMap Map;
Map.BindKey("Quit",         helio::input::Key::Escape);
Map.BindKey("Move.Forward", helio::input::Key::W);
Map.BindKey("Move.Forward", helio::input::Key::Up);     // multiple keys per action OK
Map.BindMouseButton("Fire", helio::input::MouseButton::Left);

Win.Dispatcher().SetActionMap(&Map);
Win.Dispatcher().OnActionPressed("Quit",         [&]{ Win.RequestClose(); });
Win.Dispatcher().OnActionPressed("Move.Forward", []{ /* start moving */ });
Win.Dispatcher().OnActionReleased("Move.Forward", []{ /* stop moving */ });
Win.Dispatcher().OnActionPressed("Fire",          []{ /* bang */ });
```

Action names are arbitrary strings. Bindings can change at runtime (rebind menus, mod support) without changing any subscriber code.

### 2. Raw events (for ImGui-style global capture or per-event filtering)

```cpp
Win.Dispatcher().OnEvent([&](const helio::input::InputEvent& E) {
    if (E.IsKey(helio::input::Key::F1)) ShowDebugMenu();
    if (E.Type == helio::input::InputEvent::Kind::MouseMove) {
        Camera.Yaw   += E.MoveEv.DeltaX * 0.001f;
        Camera.Pitch += E.MoveEv.DeltaY * 0.001f;
    }
});
```

Raw handlers fire for EVERY event before action routing. Use sparingly.

## Quick "I want to see it" recipe

End-to-end: window opens, ESC quits, F1 logs a message, mouse motion logs deltas. All through the dispatcher.

```cpp
#include <Core/Logging/Log.h>
#include <Input/ActionMap.h>
#include <Platform/Windows/Window.h>
#include <RHI/Public/Device.h>

int main() {
    helio::log::Init();

    helio::platform::windows::Window Win({ .Title = "Input demo", .Width = 800, .Height = 600 });
    helio::rhi::Device RHI({ .NativeWindow = Win.Native(),
                             .InitialWidth = Win.Width(), .InitialHeight = Win.Height() });

    // Action bindings.
    helio::input::ActionMap Map;
    Map.BindKey("Quit", helio::input::Key::Escape);
    Map.BindKey("Debug.Menu", helio::input::Key::F1);
    Map.BindMouseButton("Fire", helio::input::MouseButton::Left);
    Win.Dispatcher().SetActionMap(&Map);

    // Handlers.
    Win.Dispatcher().OnActionPressed("Quit",      [&]{
        HELIO_LOG_INFO("Game", "Quit pressed");
        Win.RequestClose();
    });
    Win.Dispatcher().OnActionPressed("Debug.Menu", []{
        HELIO_LOG_INFO("Game", "Debug menu requested");
    });
    Win.Dispatcher().OnActionPressed("Fire", []{
        HELIO_LOG_INFO("Game", "FIRE!");
    });
    // Raw mouse-motion logger.
    Win.Dispatcher().OnEvent([](const helio::input::InputEvent& E) {
        if (E.Type == helio::input::InputEvent::Kind::MouseMove) {
            HELIO_LOG_DEBUG("Input", "Mouse delta ({:+.1f}, {:+.1f})",
                            E.MoveEv.DeltaX, E.MoveEv.DeltaY);
        }
    });

    HELIO_LOG_INFO("Game", "Press ESC, F1, or LMB. Move the mouse for motion events.");

    while (Win.PumpEvents()) {
        if (auto* Cmd = RHI.BeginFrame()) {
            RHI.EndFrame();
        }
    }
    RHI.WaitIdle();
    helio::log::Shutdown();
}
```

Run it, press the bound keys, watch the log. ESC closes the window. F1 logs the "debug menu" line. LMB logs FIRE.

## How action routing fires

Inside `Dispatcher::Dispatch(event)`:

1. **Every** `OnEvent` handler fires with the raw event.
2. If the event is `Key` or `MouseButton` AND an `ActionMap` is bound:
   - Look up the action name for this event's code.
   - If matched, fire `OnActionPressed` handlers (if `KeyState::Pressed`) or `OnActionReleased` handlers (if `KeyState::Released`).
3. Other event kinds (MouseMove, MouseWheel, WindowResize, WindowClose) only fire raw handlers — they're not routed through actions.

## Adding new keys

`Input/Keys.h` has the standard US-layout set. To add a key (e.g. `Numpad7`):

1. Add the enum entry (in alphabetical / logical group) in `Keys.h`
2. Add the SDL → helio mapping case in `SDLKeycodeToHelio()` in `Platform/Windows/Window.cpp`

That's all — `ActionMap` and `Dispatcher` are pure-data and pick up new keys automatically.

## Gotchas

- **`SetActionMap` is a pointer.** The ActionMap must outlive the Dispatcher (typically: declare both at scope above the frame loop). Re-binding keys at runtime is fine — just mutate the same map; live subscribers don't need to re-register.
- **Action handlers don't carry the triggering event.** If you need to know which key fired (e.g. an action bound to both W and Up), use a raw `OnEvent` handler instead.
- **No analog axis support yet.** Gamepad sticks / triggers come with the gamepad pass (Phase 13 polish — SDL3 already exposes them, just need to add `GamepadAxisEvent` and bindings).
- **Window-mode keys are still text input.** `Key::A` fires for the physical A key. If you want text-entry semantics (layout-aware, shift-modified, IME-composed), you'd hook SDL's `SDL_EVENT_TEXT_INPUT` separately — V1 doesn't expose this; lands in Phase 13 as `InputEvent::TextInput`.

## What's deferred to Phase 13

- **Gamepad** (sticks, triggers, buttons, rumble)
- **Text input** (IME, layout-aware)
- **Action contexts / priorities** — currently every subscriber fires; no "UI consumed this, don't fire gameplay handlers"
- **Axis bindings** — actions for analog axes (e.g. WASD → 2D move vector)
- **Replay / record** — log events to a buffer, replay through `Dispatcher::Dispatch` for deterministic testing
- **Multi-window dispatch** — `Window::Dispatcher` is per-instance; if you ever spawn a second window (editor pane?), each has its own dispatcher
