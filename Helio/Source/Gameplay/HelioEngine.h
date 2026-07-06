/// @file HelioEngine.h
/// @brief Application framework: window + device + renderer + main loop.
///
/// The engine owns the platform/RHI/renderer plumbing and the frame loop;
/// the WORLD (scene graph) is created inside `Run()` and rendered through
/// `scene::SceneRenderer`. Input bindings live on the engine's single
/// ActionMap; camera controls go through `FlyCameraController` so the Scene
/// layer stays input-free.
#pragma once

#include <Core/Time/Clock.h>
#include <Core/Types/EngineConfig.h>

#include <Editor/EditorOverlay.h>
#include <Input/ActionMap.h>
#include <Platform/Windows/Window.h>
#include <Resource/Mesh.h>
#include <RHI/Public/Device.h>
#include <Scene/SceneRenderer.h>

#include "FlyCameraController.h"

namespace helio::gameplay
{
    class HelioEngine
    {
    public:
        explicit HelioEngine(const core::EngineConfig& Config) :
            m_Window({.Title = Config.Title, .Width = Config.Width, .Height = Config.Height}),
            m_RHI({.NativeWindow = m_Window.Native(), .InitialWidth = Config.Width, .InitialHeight = Config.Height, .EnableValidation = Config.ValidationLayers, .EnableRayTracing = Config.Raytracing}),
            m_Renderer(m_RHI, Config.Width, Config.Height),
            m_MeshSystem(m_RHI),
            m_Editor(m_Window, m_RHI, m_Renderer, rhi::Format::RGBA8_SRGB)
        {}

        void Run();

        platform::windows::Window& Window() noexcept { return m_Window; }
        rhi::Device& RHI() noexcept { return m_RHI; }
        scene::SceneRenderer& Renderer() noexcept { return m_Renderer; }
        input::ActionMap& InputActionMap() noexcept { return m_InputActionMap; }
        resource::MeshSystem& MeshSystem() noexcept { return m_MeshSystem; }
        editor::EditorOverlay& Editor() noexcept { return m_Editor; }
        const core::Clock& EngineClock() const noexcept { return m_EngineClock; }

    private:
        core::Clock m_EngineClock;

        platform::windows::Window m_Window;
        rhi::Device m_RHI;
        scene::SceneRenderer m_Renderer;
        resource::MeshSystem m_MeshSystem;
        editor::EditorOverlay m_Editor;

        input::ActionMap m_InputActionMap;
        FlyCameraController m_CameraController;
    };
} // namespace helio::gameplay
