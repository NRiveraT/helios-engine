#include "ActionMap.h"

namespace helio::input {

void ActionMap::BindKey(std::string_view Action, Key K) {
    m_keyBindings[K] = std::string{Action};
}

void ActionMap::BindMouseButton(std::string_view Action, MouseButton B) {
    m_mouseBindings[B] = std::string{Action};
}

std::string_view ActionMap::Resolve(const InputEvent& E) const {
    if (E.Type == InputEvent::Kind::Key) {
        auto It = m_keyBindings.find(E.KeyEv.Code);
        if (It != m_keyBindings.end()) return It->second;
    } else if (E.Type == InputEvent::Kind::MouseButton) {
        auto It = m_mouseBindings.find(E.MouseEv.Button);
        if (It != m_mouseBindings.end()) return It->second;
    }
    return {};
}

void ActionMap::Clear() {
    m_keyBindings.clear();
    m_mouseBindings.clear();
}

} // namespace helio::input
