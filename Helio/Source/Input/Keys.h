/// @file Keys.h
/// @brief Platform-agnostic Key + MouseButton + KeyState enums.
///
/// Game code uses `helio::input::Key::Escape` etc. — never sees SDL or Win32
/// keycodes. The Platform/Windows layer maps SDL keycodes into this enum.
#pragma once

#include <cstdint>

namespace helio::input {

enum class Key : uint16_t {
    Unknown = 0,

    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Top-row digits
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Navigation / editing
    Escape, Enter, Space, Tab, Backspace,
    Home, End, PageUp, PageDown, Insert, Delete,

    // Arrows
    Left, Right, Up, Down,

    // Modifier keys
    LeftShift, RightShift,
    LeftCtrl,  RightCtrl,
    LeftAlt,   RightAlt,
    LeftSuper, RightSuper,    // "Windows" / Cmd key

    // Punctuation (US layout)
    Grave,         // `
    Minus, Equals,
    LeftBracket, RightBracket, Backslash,
    Semicolon, Apostrophe,
    Comma, Period, Slash,

    Count,
};

enum class MouseButton : uint8_t {
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
    Count,
};

enum class KeyState : uint8_t {
    Released = 0,
    Pressed  = 1,
};

} // namespace helio::input
