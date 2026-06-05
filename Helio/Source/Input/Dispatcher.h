/// @file Dispatcher.h
/// @brief Subscribe handlers to raw input events or to symbolic actions.
///
/// Two complementary patterns:
///
/// - **Event-driven** (edge-triggered): `OnActionPressed` / `OnActionReleased`
///   fire on key transitions. Right for "Fire", "Jump", "Toggle X".
/// - **State / continuous**: `OnActionHeld` fires every frame the action is
///   held, and `IsActionHeld` / `MouseX` / `MouseDeltaX` etc. are polled
///   directly. Right for "MoveForward (WASD)" and "Look (mouse motion)".
///
/// Engine main loop wiring (typical):
///   while (running) {
///       Win.Dispatcher().BeginFrame();      // reset mouse deltas + wheel
///       Win.PumpEvents();                    // OS events → Dispatch() → state
///       Win.Dispatcher().FireHeld();         // OnActionHeld handlers
///       World.Tick(Dt);                       // actors poll IsActionHeld etc.
///       ... render ...
///   }
#pragma once

#include "Event.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    // -------------------------------------------------------------------------
    // Event-driven subscriptions
    // -------------------------------------------------------------------------

    /// Subscribe to ALL events (key, mouse, window resize, etc.). Useful
    /// for ImGui-style global capture; use action handlers for game logic.
    void OnEvent(EventHandler H);

    /// Fire on the press edge of `Action`. Action names are arbitrary strings
    /// matching whatever was registered in the `ActionMap`.
    void OnActionPressed(std::string Action, ActionHandler H);

    /// Fire on the release edge of `Action`.
    void OnActionReleased(std::string Action, ActionHandler H);

    /// Fire EVERY frame `Action` is currently held. Engine calls `FireHeld()`
    /// once per frame to drive these.
    void OnActionHeld(std::string Action, ActionHandler H);

    // -------------------------------------------------------------------------
    // State polling (any-time, no subscription needed)
    // -------------------------------------------------------------------------

    [[nodiscard]] bool IsKeyHeld(Key K) const noexcept;
    [[nodiscard]] bool IsMouseHeld(MouseButton B) const noexcept;
    /// True if any key/button bound to `Action` is currently held. Returns
    /// false if no action map is bound or the action has no bindings.
    [[nodiscard]] bool IsActionHeld(std::string_view Action) const;

    /// Mouse cursor position in window pixels (top-left = 0,0). Updated by
    /// the most recent MouseMove event.
    [[nodiscard]] float MouseX() const noexcept { return m_mouseX; }
    [[nodiscard]] float MouseY() const noexcept { return m_mouseY; }

    /// Mouse movement accumulated since the last `BeginFrame()` call. Reset
    /// each frame by the engine. Use this for fly-cam pitch/yaw, drag-gestures.
    [[nodiscard]] float MouseDeltaX() const noexcept { return m_mouseDeltaX; }
    [[nodiscard]] float MouseDeltaY() const noexcept { return m_mouseDeltaY; }

    /// Scroll wheel ticks accumulated since the last `BeginFrame()`.
    [[nodiscard]] float MouseWheelX() const noexcept { return m_wheelX; }
    [[nodiscard]] float MouseWheelY() const noexcept { return m_wheelY; }

    // -------------------------------------------------------------------------
    // Per-frame lifecycle (called by the engine main loop)
    // -------------------------------------------------------------------------

    /// Reset per-frame accumulators (mouse delta, wheel). Call at the TOP of
    /// each frame, before PumpEvents pumps new OS messages into Dispatch().
    void BeginFrame() noexcept;

    /// Fire all `OnActionHeld` handlers for any action currently held. Call
    /// once per frame after `PumpEvents`, before world tick.
    void FireHeld();

    /// Called by the platform layer for each event. You usually don't call
    /// this yourself unless you're feeding events from a synthetic source
    /// (e.g. a record/playback system or an editor).
    void Dispatch(const InputEvent& E);

    /// Drop all subscribers and reset all polled state. Doesn't touch the
    /// action map binding.
    void Clear();

private:
    ActionMap* m_actionMap{nullptr};
    std::vector<EventHandler> m_eventHandlers;
    std::unordered_map<std::string, std::vector<ActionHandler>> m_actionPressed;
    std::unordered_map<std::string, std::vector<ActionHandler>> m_actionReleased;
    std::unordered_map<std::string, std::vector<ActionHandler>> m_actionHeld;

    // Polled state — updated by Dispatch() as events flow in.
    std::unordered_set<Key>         m_keysDown;
    std::unordered_set<MouseButton> m_mouseDown;
    float m_mouseX{0.0f};
    float m_mouseY{0.0f};
    float m_mouseDeltaX{0.0f};
    float m_mouseDeltaY{0.0f};
    float m_wheelX{0.0f};
    float m_wheelY{0.0f};
};

} // namespace helio::input
