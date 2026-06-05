#include "Dispatcher.h"
#include "ActionMap.h"

#include <Core/Logging/Log.h>

namespace helio::input {

// -----------------------------------------------------------------------------
// Subscriptions
// -----------------------------------------------------------------------------
void Dispatcher::OnEvent(EventHandler H) {
    m_eventHandlers.push_back(std::move(H));
}

void Dispatcher::OnActionPressed(std::string Action, ActionHandler H) {
    m_actionPressed[std::move(Action)].push_back(std::move(H));
}

void Dispatcher::OnActionReleased(std::string Action, ActionHandler H) {
    m_actionReleased[std::move(Action)].push_back(std::move(H));
}

void Dispatcher::OnActionHeld(std::string Action, ActionHandler H) {
    m_actionHeld[std::move(Action)].push_back(std::move(H));
}

// -----------------------------------------------------------------------------
// State queries
// -----------------------------------------------------------------------------
bool Dispatcher::IsKeyHeld(Key K) const noexcept {
    return m_keysDown.contains(K);
}

bool Dispatcher::IsMouseHeld(MouseButton B) const noexcept {
    return m_mouseDown.contains(B);
}

bool Dispatcher::IsActionHeld(std::string_view Action) const {
    if (!m_actionMap) return false;
    // Check every held key/button — if any maps to `Action`, the action is held.
    for (Key K : m_keysDown) {
        if (m_actionMap->ResolveKey(K) == Action) return true;
    }
    for (MouseButton B : m_mouseDown) {
        if (m_actionMap->ResolveMouseButton(B) == Action) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// Per-frame lifecycle
// -----------------------------------------------------------------------------
void Dispatcher::BeginFrame() noexcept {
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_wheelX = 0.0f;
    m_wheelY = 0.0f;
}

void Dispatcher::FireHeld() {
    if (m_actionHeld.empty() || !m_actionMap) return;

    // Collect the set of actions currently held. Dedupe — multiple keys can
    // bind to the same action; we fire its handlers ONCE per frame even if
    // both W and Up-arrow are mashed simultaneously.
    std::unordered_set<std::string_view> Held;
    for (Key K : m_keysDown) {
        auto A = m_actionMap->ResolveKey(K);
        if (!A.empty()) Held.insert(A);
    }
    for (MouseButton B : m_mouseDown) {
        auto A = m_actionMap->ResolveMouseButton(B);
        if (!A.empty()) Held.insert(A);
    }

    // Fire registered handlers for any matching held action.
    for (auto A : Held) {
        auto It = m_actionHeld.find(std::string{A});
        if (It == m_actionHeld.end()) continue;
        for (auto& H : It->second) H();
    }
}

// -----------------------------------------------------------------------------
// Event ingestion (called by platform layer per OS event)
// -----------------------------------------------------------------------------
void Dispatcher::Dispatch(const InputEvent& E) {
    // 1. Update polled state BEFORE firing subscribers, so handlers and any
    //    subsequent poll see the freshest data.
    switch (E.Type) {
        case InputEvent::Kind::Key:
            if (E.KeyEv.State == KeyState::Pressed)  m_keysDown.insert(E.KeyEv.Code);
            if (E.KeyEv.State == KeyState::Released) m_keysDown.erase (E.KeyEv.Code);
            break;
        case InputEvent::Kind::MouseButton:
            if (E.MouseEv.State == KeyState::Pressed)  m_mouseDown.insert(E.MouseEv.Button);
            if (E.MouseEv.State == KeyState::Released) m_mouseDown.erase (E.MouseEv.Button);
            m_mouseX = E.MouseEv.X;
            m_mouseY = E.MouseEv.Y;
            break;
        case InputEvent::Kind::MouseMove:
            m_mouseX = E.MoveEv.X;
            m_mouseY = E.MoveEv.Y;
            m_mouseDeltaX += E.MoveEv.DeltaX;
            m_mouseDeltaY += E.MoveEv.DeltaY;
            break;
        case InputEvent::Kind::MouseWheel:
            m_wheelX += E.WheelEv.DeltaX;
            m_wheelY += E.WheelEv.DeltaY;
            break;
        case InputEvent::Kind::WindowResize:
        case InputEvent::Kind::WindowClose:
            // No polled-state update; raw subscribers handle these.
            break;
    }

    // 2. Raw subscribers — get every event verbatim.
    for (auto& H : m_eventHandlers) {
        H(E);
    }

    // 3. Action routing for key + mouse events when an action map is bound.
    if (!m_actionMap) return;
    if (E.Type != InputEvent::Kind::Key && E.Type != InputEvent::Kind::MouseButton) return;

    auto Action = m_actionMap->Resolve(E);
    if (Action.empty()) return;

    const KeyState State = (E.Type == InputEvent::Kind::Key) ? E.KeyEv.State : E.MouseEv.State;
    auto& Bucket = (State == KeyState::Pressed) ? m_actionPressed : m_actionReleased;
    auto It = Bucket.find(std::string{Action});
    if (It == Bucket.end()) return;
    for (auto& H : It->second) H();
}

void Dispatcher::Clear() {
    m_eventHandlers.clear();
    m_actionPressed.clear();
    m_actionReleased.clear();
    m_actionHeld.clear();
    m_keysDown.clear();
    m_mouseDown.clear();
    m_mouseX = m_mouseY = 0.0f;
    m_mouseDeltaX = m_mouseDeltaY = 0.0f;
    m_wheelX = m_wheelY = 0.0f;
}

} // namespace helio::input
