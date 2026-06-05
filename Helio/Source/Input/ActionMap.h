/// @file ActionMap.h
/// @brief Bind keys + mouse buttons to symbolic action names.
///
/// Example:
///   ActionMap Map;
///   Map.BindKey("Quit",         Key::Escape);
///   Map.BindKey("Move.Forward", Key::W);
///   Map.BindMouseButton("Fire", MouseButton::Left);
///
/// The `Dispatcher` resolves incoming events through the bound action map and
/// invokes any `OnActionPressed` / `OnActionReleased` handlers registered for
/// the matched action name.
#pragma once

#include "Event.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace helio::input {

class ActionMap {
public:
    /// Bind a key to fire `Action` when pressed / released. Multiple keys can
    /// map to the same action (e.g. both `Up` and `W` → "Move.Forward").
    /// One key can only map to one action — re-binding overwrites.
    void BindKey(std::string_view Action, Key K);
    void BindMouseButton(std::string_view Action, MouseButton B);

    /// Reverse lookup: which action (if any) does this event trigger?
    /// Returns empty `string_view` if no binding matches.
    [[nodiscard]] std::string_view Resolve(const InputEvent& E) const;

    /// Direct lookups — same as `Resolve()` but skip the InputEvent shape.
    /// Used by `Dispatcher::FireHeld()` and `Dispatcher::IsActionHeld()` to
    /// translate currently-held keys/buttons into action names.
    [[nodiscard]] std::string_view ResolveKey(Key K) const;
    [[nodiscard]] std::string_view ResolveMouseButton(MouseButton B) const;

    /// Drop all bindings.
    void Clear();

private:
    std::unordered_map<Key, std::string>         m_keyBindings;
    std::unordered_map<MouseButton, std::string> m_mouseBindings;
};

} // namespace helio::input
