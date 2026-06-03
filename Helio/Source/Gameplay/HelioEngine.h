#pragma once

#include <Mesh.h>
#include <Overlay/Overlay.h>
#include <Platform/Windows/Window.h>
#include <Public/Device.h>

#include "Types/EngineConfig.h"
#include "World/HelioRenderer.h"

namespace helio::gameplay
{
    class HelioEngine
    {
    public:
        explicit HelioEngine(const EngineConfig& Config) :
            m_Window({.Title = Config.Title, .Width = Config.Width, .Height = Config.Height}),
            m_RHI({.NativeWindow = m_Window.Native(), .InitialWidth = Config.Width, .InitialHeight = Config.Height, .EnableValidation = Config.ValidationLayers, .EnableRayTracing = Config.Raytracing}),
            m_Renderer(m_RHI, Config),
            m_MeshSystem(m_RHI)
        {}

        void Run();

        platform::windows::Window& Window() { return m_Window; }
        rhi::Device& RHI() { return m_RHI; }
        HelioRenderer& Renderer() noexcept { return m_Renderer; }
        
        resource::MeshSystem& MeshSystem() { return m_MeshSystem; }

    private:
        platform::windows::Window m_Window;
        rhi::Device m_RHI;
        HelioRenderer m_Renderer;
        resource::MeshSystem m_MeshSystem;
    };
}
