/// @file Dispatcher.h
/// @brief Subscribe handlers to raw input events or to symbolic actions.
///
/// Typical wiring:
///   ActionMap Map;
///   Map.BindKey("Quit", Key::Escape);
///   Dispatcher Disp;
///   Disp.SetActionMap(&Map);
///   Disp.OnActionPressed("Quit", [&]{ Win.RequestClose(); });
///
/// Then the platform layer calls `Disp.Dispatch(event)` for each incoming
/// `InputEvent`, which:
///   1. Fires every `OnEvent` subscriber.
///   2. If `Type == Key` or `Type == MouseButton` and an action map is bound,
///      resolves the event to an action name and fires
///      `OnActionPressed`/`OnActionReleased` subscribers for that action.
#pragma once

#include "Event.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace helio::input {

class ActionMap;

class Dispatcher {
public:
    using EventHandler  = std::function<void(const InputEvent&)>;
    using ActionHandler = std::function<void()>;

    /// Set the action map used to translate Key/MouseButton events into
    /// action callbacks. Pass `nullptr` to disable action routing entirely
    /// (raw event handlers still fire).
    void SetActionMap(ActionMap* M) noexcept { m_actionMap = M; }

    /// Subscribe to ALL events (key, mouse, window resize, etc.). Useful
    /// for ImGui-style global capture; use action handlers for game logic.
    void OnEvent(EventHandler H);

    /// Fire on the press edge of `Action`. Action names are arbitrary strings
    /// matching whatever was registered in the `ActionMap`.
    void OnActionPressed(std::string Action, ActionHandler H);

    /// Fire on the release edge of `Action`.
    void OnActionReleased(std::string Action, ActionHandler H);

    /// Called by the platform layer for each event. You usually don't call
    /// this yourself unless you're feeding events from a synthetic source
    /// (e.g. a record/playback system or an editor).
    void Dispatch(const InputEvent& E);

    /// Drop all subscribers. Doesn't touch the action map.
    void Clear();

private:
    ActionMap* m_actionMap{nullptr};
    std::vector<EventHandler> m_eventHandlers;
    std::unordered_map<std::string, std::vector<ActionHandler>> m_actionPressed;
    std::unordered_map<std::string, std::vector<ActionHandler>> m_actionReleased;
};

} // namespace helio::input
