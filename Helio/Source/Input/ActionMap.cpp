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
        return ResolveKey(E.KeyEv.Code);
    } else if (E.Type == InputEvent::Kind::MouseButton) {
        return ResolveMouseButton(E.MouseEv.Button);
    }
    return {};
}

std::string_view ActionMap::ResolveKey(Key K) const {
    auto It = m_keyBindings.find(K);
    return (It != m_keyBindings.end()) ? std::string_view{It->second} : std::string_view{};
}

std::string_view ActionMap::ResolveMouseButton(MouseButton B) const {
    auto It = m_mouseBindings.find(B);
    return (It != m_mouseBindings.end()) ? std::string_view{It->second} : std::string_view{};
}

void ActionMap::Clear() {
    m_keyBindings.clear();
    m_mouseBindings.clear();
}

} // namespace helio::input
