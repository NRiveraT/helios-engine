/// @file EditorOverlay.h
/// @brief Toggleable Dear ImGui (docking) editor drawn over the running game.
///
/// F1 toggles visibility. While visible, a fullscreen dockspace (passthrough
/// central node — the 3D scene stays visible and clickable through the hole)
/// hosts three panels:
///   - Scene:     the world's actor tree (parent/child), selection, delete.
///   - Inspector: name / local transform (position, Euler-degree rotation,
///                scale) + type-specific sections (material, light, camera).
///   - Stats:     CPU/GPU frame times, actor count.
///
/// Input routing: the overlay installs a native-event hook on the Window
/// (`Window::SetNativeEventHook`) that feeds every SDL event to ImGui's SDL3
/// backend and CONSUMES keyboard/mouse events whenever the UI wants them —
/// gameplay input never sees clicks on editor windows. The engine also polls
/// `WantsInput()` to suspend the fly-camera controller while the UI has
/// focus. Rendering goes through `ImGuiRenderer` (bindless, RHI-only) via
/// the SceneRenderer's overlay hook.
#pragma once

#include "ImGuiRenderer.h"

#include <cstdint>

struct ImGuiContext;

namespace helio::platform::windows { class Window; }
namespace helio::scene
{
    class Actor;
    class HelioWorld;
    class SceneRenderer;
}

namespace helio::editor
{
    class EditorOverlay
    {
    public:
        EditorOverlay(platform::windows::Window& Win, rhi::Device& Dev,
                      scene::SceneRenderer& Renderer, rhi::Format TargetFormat);
        ~EditorOverlay();

        EditorOverlay(const EditorOverlay&) = delete;
        EditorOverlay& operator=(const EditorOverlay&) = delete;

        /// The world whose actor tree the Scene panel shows. May be rebound
        /// between play sessions; pass nullptr to show an empty tree.
        void SetWorld(scene::HelioWorld* World) noexcept { m_World = World; }

        void SetVisible(bool Visible);
        void Toggle() { SetVisible(!m_Visible); }
        [[nodiscard]] bool IsVisible() const noexcept { return m_Visible; }

        /// True while the UI wants the mouse or keyboard — the engine gates
        /// the camera controller (and any other gameplay input consumer) on
        /// this.
        [[nodiscard]] bool WantsInput() const;

        /// Build this frame's UI. Call once per frame after the window pumped
        /// events and before `SceneRenderer::Render`. No-op while hidden.
        void BeginFrame();

        /// SceneRenderer overlay hook — declares the ImGui pass onto the
        /// frame's color target. No-op unless `BeginFrame` ran this frame.
        void Render(render::RenderGraph& Rg, rhi::TextureHandle Target,
                    uint32_t Width, uint32_t Height);

    private:
        bool HandleNativeEvent(const void* NativeEvent);

        void DrawDockspace();
        void DrawScenePanel();
        void DrawSceneNode(scene::Actor& A);
        void DrawInspectorPanel();
        void DrawStatsPanel();

        [[nodiscard]] scene::Actor* ResolveSelected() const;

        platform::windows::Window* m_Window;
        rhi::Device* m_Dev;
        scene::SceneRenderer* m_SceneRenderer;
        scene::HelioWorld* m_World = nullptr;

        ImGuiContext* m_Ctx = nullptr;
        ImGuiRenderer m_Renderer;

        bool m_Visible = true;
        bool m_FrameActive = false;
        bool m_BuildDefaultLayout = false;

        uint64_t m_SelectedId = 0;

        // Euler-angle edit cache: quat->Euler->quat round trips jitter near
        // the poles, so while the rotation widget is active we edit these
        // cached angles and only derive the quaternion from them.
        float m_EulerCache[3] = {0, 0, 0};
        uint64_t m_EulerCacheId = 0;
        bool m_EulerEditing = false;
    };
} // namespace helio::editor
