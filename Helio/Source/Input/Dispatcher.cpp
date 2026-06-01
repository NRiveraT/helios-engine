#include "Dispatcher.h"
#include "ActionMap.h"

#include <Core/Logging/Log.h>

namespace helio::input {

void Dispatcher::OnEvent(EventHandler H) {
    m_eventHandlers.push_back(std::move(H));
}

void Dispatcher::OnActionPressed(std::string Action, ActionHandler H) {
    m_actionPressed[std::move(Action)].push_back(std::move(H));
}

void Dispatcher::OnActionReleased(std::string Action, ActionHandler H) {
    m_actionReleased[std::move(Action)].push_back(std::move(H));
}

void Dispatcher::Dispatch(const InputEvent& E) {
    // 1. Raw subscribers — get everything.
    for (auto& H : m_eventHandlers) {
        H(E);
    }

    // 2. Action routing for key + mouse events when an action map is bound.
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
}

} // namespace helio::input
