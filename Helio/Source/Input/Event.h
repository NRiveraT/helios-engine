/// @file Event.h
/// @brief Tagged-union `InputEvent` covering keyboard, mouse, and window state.
///
/// The platform layer (`Platform/Windows/Window.cpp`) translates raw SDL events
/// into these structs and dispatches them through `helio::input::Dispatcher`.
#pragma once

#include "Keys.h"

#include <cstdint>

namespace helio::input {

struct KeyEvent {
    Key      Code{Key::Unknown};
    KeyState State{KeyState::Released};
    bool     Repeat{false};
};

struct MouseButtonEvent {
    MouseButton Button{MouseButton::Left};
    KeyState    State{KeyState::Released};
    float       X{0.0f};
    float       Y{0.0f};
};

struct MouseMoveEvent {
    float X{0.0f};
    float Y{0.0f};
    float DeltaX{0.0f};
    float DeltaY{0.0f};
};

struct MouseWheelEvent {
    float DeltaX{0.0f};
    float DeltaY{0.0f};
};

struct WindowResizeEvent {
    int Width{0};
    int Height{0};
};

struct WindowCloseEvent {};

/// Tagged union — read `Type` first, then access the matching `*Ev` field.
/// Field names are suffixed `Ev` so the union doesn't shadow the type names
/// in the enclosing namespace (e.g. you can still write `helio::input::Key`
/// or `helio::input::MouseButton` inside member functions).
struct InputEvent {
    enum class Kind : uint8_t {
        Key,
        MouseButton,
        MouseMove,
        MouseWheel,
        WindowResize,
        WindowClose,
    } Type{Kind::Key};

    union {
        KeyEvent           KeyEv;
        MouseButtonEvent   MouseEv;
        MouseMoveEvent     MoveEv;
        MouseWheelEvent    WheelEv;
        WindowResizeEvent  ResizeEv;
        WindowCloseEvent   CloseEv;
    };

    /// Convenience: did this event just press key `K`?
    [[nodiscard]] constexpr bool IsKey(Key K, KeyState S = KeyState::Pressed) const noexcept {
        return Type == Kind::Key && KeyEv.Code == K && KeyEv.State == S;
    }

    /// Convenience: did this event just press mouse button `B`?
    [[nodiscard]] constexpr bool IsMouse(MouseButton B, KeyState S = KeyState::Pressed) const noexcept {
        return Type == Kind::MouseButton && MouseEv.Button == B && MouseEv.State == S;
    }
};

} // namespace helio::input
