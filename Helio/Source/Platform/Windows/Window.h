/// @file Window.h
/// @brief SDL3-backed OS window with Vulkan-ready surface flag.
///
/// Owns SDL initialization (refcounted via a static guard) so multiple Windows
/// can be created and destroyed safely. The native SDL_Window* is exposed for
/// the RHI to create a VkSurfaceKHR from in Phase 4.
#pragma once

#include <Input/Dispatcher.h>

#include <functional>
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

    /// Hook invoked for EVERY native SDL event BEFORE it is translated and
    /// dispatched to gameplay input. `NativeEvent` is a `const SDL_Event*`
    /// (type-erased so SDL stays out of this public header — the installer
    /// links SDL and casts it back). Return true to CONSUME the event: it
    /// will not reach the input dispatcher. Window-close is never consumable
    /// (close intent always registers). One hook slot — installing replaces
    /// the previous hook; pass an empty function to remove.
    ///
    /// This is the editor/UI capture point: Dear ImGui's SDL3 backend feeds
    /// from here and swallows keyboard/mouse while its widgets have focus.
    using NativeEventHook = std::function<bool(const void* NativeEvent)>;
    void SetNativeEventHook(NativeEventHook Hook) { m_nativeEventHook = std::move(Hook); }

    // -------------------------------------------------------------------------
    // Mouse capture / relative mode
    //
    // For free-look / fly-cam style controls you need relative mouse mode:
    // - The OS cursor is hidden
    // - The cursor stays locked to the window (can't drift to other monitors
    //   or hit screen edges)
    // - Mouse events emit DeltaX/DeltaY but never absolute X/Y movement
    // - You can keep dragging in one direction forever, no edge-clamp stalls
    //
    // Without this, mouse-look stops the moment the cursor reaches the screen
    // edge — you can't sustain a long camera rotation.
    //
    // Game code toggles capture as needed (e.g. press RMB → SetMouseCaptured(true)
    // → release RMB → SetMouseCaptured(false)). Esc-to-uncapture is also a
    // common pattern; wire your own action handler for that.
    // -------------------------------------------------------------------------

    /// Enable/disable relative mouse mode + cursor hiding. Idempotent (safe
    /// to call repeatedly with the same value).
    void SetMouseCaptured(bool Captured) noexcept;

    /// True if relative mouse mode is currently active.
    [[nodiscard]] bool IsMouseCaptured() const noexcept { return m_mouseCaptured; }

private:
    SDL_Window* m_window{nullptr};
    bool m_shouldClose{false};
    bool m_mouseCaptured{false};
    input::Dispatcher m_dispatcher;
    NativeEventHook m_nativeEventHook;
};

} // namespace helio::platform::windows
