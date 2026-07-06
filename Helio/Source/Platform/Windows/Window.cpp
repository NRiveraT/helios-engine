#include "Window.h"

#include <Core/Logging/Log.h>
#include <Core/Assert/Assert.h>
#include <Core/Profile/Profile.h>
#include <Input/Event.h>
#include <Input/Keys.h>

#include <SDL3/SDL.h>

namespace helio::platform::windows {

namespace {

// SDL keycode -> helio::input::Key. Returns Key::Unknown for unmapped keys.
input::Key SDLKeycodeToHelio(SDL_Keycode K) {
    switch (K) {
        // Letters
        case SDLK_A: return input::Key::A;  case SDLK_B: return input::Key::B;
        case SDLK_C: return input::Key::C;  case SDLK_D: return input::Key::D;
        case SDLK_E: return input::Key::E;  case SDLK_F: return input::Key::F;
        case SDLK_G: return input::Key::G;  case SDLK_H: return input::Key::H;
        case SDLK_I: return input::Key::I;  case SDLK_J: return input::Key::J;
        case SDLK_K: return input::Key::K;  case SDLK_L: return input::Key::L;
        case SDLK_M: return input::Key::M;  case SDLK_N: return input::Key::N;
        case SDLK_O: return input::Key::O;  case SDLK_P: return input::Key::P;
        case SDLK_Q: return input::Key::Q;  case SDLK_R: return input::Key::R;
        case SDLK_S: return input::Key::S;  case SDLK_T: return input::Key::T;
        case SDLK_U: return input::Key::U;  case SDLK_V: return input::Key::V;
        case SDLK_W: return input::Key::W;  case SDLK_X: return input::Key::X;
        case SDLK_Y: return input::Key::Y;  case SDLK_Z: return input::Key::Z;
        // Digits
        case SDLK_0: return input::Key::Num0;  case SDLK_1: return input::Key::Num1;
        case SDLK_2: return input::Key::Num2;  case SDLK_3: return input::Key::Num3;
        case SDLK_4: return input::Key::Num4;  case SDLK_5: return input::Key::Num5;
        case SDLK_6: return input::Key::Num6;  case SDLK_7: return input::Key::Num7;
        case SDLK_8: return input::Key::Num8;  case SDLK_9: return input::Key::Num9;
        // Function
        case SDLK_F1:  return input::Key::F1;   case SDLK_F2:  return input::Key::F2;
        case SDLK_F3:  return input::Key::F3;   case SDLK_F4:  return input::Key::F4;
        case SDLK_F5:  return input::Key::F5;   case SDLK_F6:  return input::Key::F6;
        case SDLK_F7:  return input::Key::F7;   case SDLK_F8:  return input::Key::F8;
        case SDLK_F9:  return input::Key::F9;   case SDLK_F10: return input::Key::F10;
        case SDLK_F11: return input::Key::F11;  case SDLK_F12: return input::Key::F12;
        // Navigation
        case SDLK_ESCAPE:    return input::Key::Escape;
        case SDLK_RETURN:    return input::Key::Enter;
        case SDLK_SPACE:     return input::Key::Space;
        case SDLK_TAB:       return input::Key::Tab;
        case SDLK_BACKSPACE: return input::Key::Backspace;
        case SDLK_HOME:      return input::Key::Home;
        case SDLK_END:       return input::Key::End;
        case SDLK_PAGEUP:    return input::Key::PageUp;
        case SDLK_PAGEDOWN:  return input::Key::PageDown;
        case SDLK_INSERT:    return input::Key::Insert;
        case SDLK_DELETE:    return input::Key::Delete;
        // Arrows
        case SDLK_LEFT:      return input::Key::Left;
        case SDLK_RIGHT:     return input::Key::Right;
        case SDLK_UP:        return input::Key::Up;
        case SDLK_DOWN:      return input::Key::Down;
        // Modifiers
        case SDLK_LSHIFT: return input::Key::LeftShift;   case SDLK_RSHIFT: return input::Key::RightShift;
        case SDLK_LCTRL:  return input::Key::LeftCtrl;    case SDLK_RCTRL:  return input::Key::RightCtrl;
        case SDLK_LALT:   return input::Key::LeftAlt;     case SDLK_RALT:   return input::Key::RightAlt;
        case SDLK_LGUI:   return input::Key::LeftSuper;   case SDLK_RGUI:   return input::Key::RightSuper;
        // Punctuation
        case SDLK_GRAVE:        return input::Key::Grave;
        case SDLK_MINUS:        return input::Key::Minus;
        case SDLK_EQUALS:       return input::Key::Equals;
        case SDLK_LEFTBRACKET:  return input::Key::LeftBracket;
        case SDLK_RIGHTBRACKET: return input::Key::RightBracket;
        case SDLK_BACKSLASH:    return input::Key::Backslash;
        case SDLK_SEMICOLON:    return input::Key::Semicolon;
        case SDLK_APOSTROPHE:   return input::Key::Apostrophe;
        case SDLK_COMMA:        return input::Key::Comma;
        case SDLK_PERIOD:       return input::Key::Period;
        case SDLK_SLASH:        return input::Key::Slash;
        default:                return input::Key::Unknown;
    }
}

input::MouseButton SDLMouseButtonToHelio(Uint8 B) {
    switch (B) {
        case SDL_BUTTON_LEFT:   return input::MouseButton::Left;
        case SDL_BUTTON_RIGHT:  return input::MouseButton::Right;
        case SDL_BUTTON_MIDDLE: return input::MouseButton::Middle;
        case SDL_BUTTON_X1:     return input::MouseButton::X1;
        case SDL_BUTTON_X2:     return input::MouseButton::X2;
        default:                return input::MouseButton::Left;
    }
}

} // namespace

namespace {

/// SDL_Init/SDL_Quit guard. First Window construction initializes; static
/// destruction calls SDL_Quit.
struct SDLInitGuard {
    bool Ok{false};
    SDLInitGuard() {
        Ok = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        if (!Ok) {
            HELIO_LOG_CRITICAL("Platform", "SDL_Init failed: {}", SDL_GetError());
        }
    }
    ~SDLInitGuard() {
        if (Ok) SDL_Quit();
    }
};

SDLInitGuard& EnsureSDL() {
    static SDLInitGuard G;
    HELIO_CHECK(G.Ok);
    return G;
}

const char* KeyName(SDL_Keycode K) {
    switch (K) {
        case SDLK_UP:     return "Up";
        case SDLK_DOWN:   return "Down";
        case SDLK_LEFT:   return "Left";
        case SDLK_RIGHT:  return "Right";
        case SDLK_ESCAPE: return "Escape";
        case SDLK_SPACE:  return "Space";
        case SDLK_RETURN: return "Enter";
        default:          return SDL_GetKeyName(K);
    }
}

} // namespace

Window::Window(const WindowConfig& Config) {
    (void)EnsureSDL();

    SDL_WindowFlags Flags = SDL_WINDOW_RESIZABLE;
    if (Config.RequestVulkan) {
        Flags |= SDL_WINDOW_VULKAN;
    }

    m_window = SDL_CreateWindow(Config.Title.c_str(), Config.Width, Config.Height, Flags);
    if (!m_window) {
        HELIO_LOG_CRITICAL("Platform", "SDL_CreateWindow failed: {}", SDL_GetError());
        HELIO_CHECK(m_window);
    }

    HELIO_LOG_INFO("Platform", "Window '{}' {}x{} opened (Vulkan={})", Config.Title, Config.Width, Config.Height, Config.RequestVulkan);
}

Window::~Window() {
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        HELIO_LOG_INFO("Platform", "Window destroyed.");
    }
}

void Window::SetMouseCaptured(bool Captured) noexcept {
    if (!m_window || m_mouseCaptured == Captured) return;

    if (!SDL_SetWindowRelativeMouseMode(m_window, Captured)) {
        HELIO_LOG_WARN("Platform", "SDL_SetWindowRelativeMouseMode({}) failed: {}",
                       Captured, SDL_GetError());
        return;
    }
    m_mouseCaptured = Captured;
    HELIO_LOG_DEBUG("Platform", "Mouse capture {}", Captured ? "ON" : "OFF");
}

bool Window::PumpEvents() {
    HELIO_PROFILE_ZONE("Window::PumpEvents");

    SDL_Event Ev;
    while (SDL_PollEvent(&Ev)) {
        // Native hook (editor UI) sees every event first and may consume it —
        // EXCEPT close intent, which must always register.
        const bool IsCloseEvent =
            Ev.type == SDL_EVENT_QUIT || Ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        if (m_nativeEventHook && m_nativeEventHook(&Ev) && !IsCloseEvent) {
            continue;
        }

        input::InputEvent Out{};
        bool Push = true;

        switch (Ev.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_shouldClose = true;
                HELIO_LOG_INFO("Input", "Close requested.");
                Out.Type = input::InputEvent::Kind::WindowClose;
                Out.CloseEv = {};
                break;

            case SDL_EVENT_KEY_DOWN:
                // HELIO_LOG_DEBUG("Input", "Key down: {}", KeyName(Ev.key.key));
                Out.Type = input::InputEvent::Kind::Key;
                Out.KeyEv = { SDLKeycodeToHelio(Ev.key.key), input::KeyState::Pressed, Ev.key.repeat != 0 };
                break;

            case SDL_EVENT_KEY_UP:
                // HELIO_LOG_DEBUG("Input", "Key up: {}", KeyName(Ev.key.key));
                Out.Type = input::InputEvent::Kind::Key;
                Out.KeyEv = { SDLKeycodeToHelio(Ev.key.key), input::KeyState::Released, false };
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // HELIO_LOG_DEBUG("Input", "Mouse button {} down at ({:.0f},{:.0f})", static_cast<int>(Ev.button.button), Ev.button.x, Ev.button.y);
                Out.Type = input::InputEvent::Kind::MouseButton;
                Out.MouseEv = { SDLMouseButtonToHelio(Ev.button.button), input::KeyState::Pressed,
                                Ev.button.x, Ev.button.y };
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                // HELIO_LOG_DEBUG("Input", "Mouse button {} up at ({:.0f},{:.0f})", static_cast<int>(Ev.button.button), Ev.button.x, Ev.button.y);
                Out.Type = input::InputEvent::Kind::MouseButton;
                Out.MouseEv = { SDLMouseButtonToHelio(Ev.button.button), input::KeyState::Released,
                                Ev.button.x, Ev.button.y };
                break;

            case SDL_EVENT_MOUSE_MOTION:
                Out.Type = input::InputEvent::Kind::MouseMove;
                Out.MoveEv = { Ev.motion.x, Ev.motion.y, Ev.motion.xrel, Ev.motion.yrel };
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                Out.Type = input::InputEvent::Kind::MouseWheel;
                Out.WheelEv = { Ev.wheel.x, Ev.wheel.y };
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                HELIO_LOG_INFO("Platform", "Window resized to {}x{}", Ev.window.data1, Ev.window.data2);
                Out.Type = input::InputEvent::Kind::WindowResize;
                Out.ResizeEv = { Ev.window.data1, Ev.window.data2 };
                break;

            default:
                Push = false;
                break;
        }

        if (Push) m_dispatcher.Dispatch(Out);
    }

    return !m_shouldClose;
}

int Window::Width() const noexcept {
    int W = 0, H = 0;
    if (m_window) SDL_GetWindowSize(m_window, &W, &H);
    return W;
}

int Window::Height() const noexcept {
    int W = 0, H = 0;
    if (m_window) SDL_GetWindowSize(m_window, &W, &H);
    return H;
}

} // namespace helio::platform::windows
