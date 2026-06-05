#include "HelioEngine.h"

#include <MeshPrimitives.h>
#include <RenderGraph.h>
#include <Debug/DebugDraw.h>
#include <Logging/Log.h>
#include <Profile/Profile.h>
#include <Time/Clock.h>

#include "Actors/DirectionalLight.h"
#include "Actors/StaticMeshActor.h"
#include "Gameplay/World/HelioWorld.h"

using namespace helio;

namespace helio::gameplay
{
    void HelioEngine::Run()
    {
        HELIO_PROFILE_ZONE("Startup");

        Window().Dispatcher().SetActionMap(&m_InputActionMap);
        
        HelioWorld World(*this);
        m_Renderer.SetWorld(World);

        auto data = primitives::Cube(1.0f);
        StaticMeshActor* Cuber = World.SpawnActor<StaticMeshActor>(World, MeshSystem().CreateMesh({.Data = &data, .DebugName = "Cuber"}));
        Cuber->GetMaterial().AlbedoTint = float3(0.0f, 0.0f, 1.0f);
        Cuber->GetMaterial().Metallic = 1.0;
        Cuber->GetMaterial().Roughness = 0.1;

        auto sphere_data = primitives::Sphere(0.5f, 32, 16);
        StaticMeshActor* SphereGuy = World.SpawnActor<StaticMeshActor>(World, MeshSystem().CreateMesh({.Data = &sphere_data, .DebugName = "SphereGuy"}));
        SphereGuy->GetTransform().Position = float3(-2, 0, 0);
        SphereGuy->GetMaterial().AlbedoTint = float3(1.0f, 0.0f, 0.0f);
        SphereGuy->GetMaterial().Roughness = 0.2;

        auto cylinder_data = primitives::Cylinder(0.5f, 1, 16);
        StaticMeshActor* CylinderDude = World.SpawnActor<StaticMeshActor>(World, MeshSystem().CreateMesh({.Data = &cylinder_data, .DebugName = "CylinderDude"}));
        CylinderDude->GetTransform().Position = float3(2, 0, 0);
        CylinderDude->GetMaterial().AlbedoTint = float3(0.0f, 1.0f, 0.0f);
        CylinderDude->GetMaterial().Roughness = 0.6;

        auto plane_data = primitives::Plane(10.f, 10.f);
        StaticMeshActor* PlaneMan = World.SpawnActor<StaticMeshActor>(World, MeshSystem().CreateMesh({.Data = &plane_data, .DebugName = "PlaneMan"}));
        PlaneMan->GetTransform().Position = float3(0, -1, 0);
        PlaneMan->GetMaterial().AlbedoTint = float3(1.f);
        
        Camera* camera = World.SpawnActor<Camera>(World, m_Window.Width(), m_Window.Height());
        camera->GetTransform().Position = float3(0, 0, -10);
        m_Renderer.SetRenderingCamera(camera);

        DirectionalLight* light = World.SpawnActor<DirectionalLight>(World);
        light->GetTransform().RotateEuler(0, 0, 0);
        
        float time = 0;
        while (true)
        {
            HELIO_PROFILE_FRAME();
            HELIO_PROFILE_ZONE("Frame");
            const float Dt = static_cast<float>(m_EngineClock.Tick());

            m_Window.Dispatcher().BeginFrame();
            helio::debug::Tick(Dt);   // decay debug-draw lifetimes — without this, lifetime-0 items live forever

            if (!m_Window.PumpEvents())
            {
                break;
            }

            m_Window.Dispatcher().FireHeld();
            
            World.Tick(Dt);
            time += Dt;
            
            Cuber->GetTransform().RotateAxis(float3(0, 1, 0), 3.f * Dt);
            SphereGuy->GetTransform().Position.y = (float)sin(time);

            light->GetTransform().RotateAxis(float3(1, 0, 0), sin(time) * Dt);
            
            m_Renderer.Render();
        }
        m_Renderer.WaitIdle();
    }
}
