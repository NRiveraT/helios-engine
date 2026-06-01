/// @file Window.h
/// @brief SDL3-backed OS window with Vulkan-ready surface flag.
///
/// Owns SDL initialization (refcounted via a static guard) so multiple Windows
/// can be created and destroyed safely. The native SDL_Window* is exposed for
/// the RHI to create a VkSurfaceKHR from in Phase 4.
#pragma once

#include <Input/Dispatcher.h>

#include <string>

struct SDL_Window;

namespace helio::platform::windows {

struct WindowConfig {
    std::string Title{"Helio"};
    int Width{1280};
    int Height{720};
    /// Pass SDL_WINDOW_VULKAN to SDL_CreateWindow so the Vulkan loader can
    /// surface this window later. Default true.
    bool RequestVulkan{true};
};

class Window {
public:
    explicit Window(const WindowConfig& Config);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// Process pending OS messages. Returns false if the user requested close
    /// (window close button, application quit, or ESC pressed).
    ///
    /// In Phase 3 this directly logs key and mouse events to the Input
    /// log category. Phase 10 routes them through the Helio.Input dispatcher.
    [[nodiscard]] bool PumpEvents();

    /// Native SDL window handle. Used by the Vulkan RHI to create a surface.
    [[nodiscard]] SDL_Window* Native() const noexcept { return m_window; }

    /// Current window dimensions in pixels.
    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;

    /// Programmatically request closure (next PumpEvents() will return false).
    void RequestClose() noexcept { m_shouldClose = true; }

    /// Get the input dispatcher this window pumps events through. Subscribe
    /// to actions / raw events here:
    ///   Win.Dispatcher().OnActionPressed("Quit", [&]{ Win.RequestClose(); });
    [[nodiscard]] input::Dispatcher& Dispatcher() noexcept { return m_dispatcher; }

private:
    SDL_Window* m_window{nullptr};
    bool m_shouldClose{false};
    input::Dispatcher m_dispatcher;
};

} // namespace helio::platform::windows
