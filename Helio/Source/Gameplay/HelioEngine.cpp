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
        StaticMeshActor* Cuber = World.SpawnActor<StaticMeshActor>(MeshSystem().CreateMesh({.Data = &data, .DebugName = "Cuber"}));
        Cuber->GetMaterial().AlbedoTint = float3(0.0f, 0.0f, 1.0f);
        Cuber->GetMaterial().Metallic = 1.0f;
        Cuber->GetMaterial().Roughness = 0.1f;

        auto sphere_data = primitives::Sphere(0.5f, 32, 16);
        StaticMeshActor* SphereGuy = World.SpawnActor<StaticMeshActor>(MeshSystem().CreateMesh({.Data = &sphere_data, .DebugName = "SphereGuy"}));
        SphereGuy->GetTransform().Position = float3(-2, 0, 0);
        SphereGuy->GetMaterial().AlbedoTint = float3(1.0f, 0.0f, 0.0f);
        SphereGuy->GetMaterial().Roughness = 0.2f;

        auto cylinder_data = primitives::Cylinder(0.5f, 1, 16);
        StaticMeshActor* CylinderDude = World.SpawnActor<StaticMeshActor>(MeshSystem().CreateMesh({.Data = &cylinder_data, .DebugName = "CylinderDude"}));
        CylinderDude->GetTransform().Position = float3(2, 0, 0);
        CylinderDude->GetMaterial().AlbedoTint = float3(0.0f, 1.0f, 0.0f);
        CylinderDude->GetMaterial().Roughness = 0.6f;

        auto plane_data = primitives::Plane(10.f, 10.f);
        StaticMeshActor* PlaneMan = World.SpawnActor<StaticMeshActor>(MeshSystem().CreateMesh({.Data = &plane_data, .DebugName = "PlaneMan"}));
        PlaneMan->GetTransform().Position = float3(0, -1, 0);
        PlaneMan->GetMaterial().AlbedoTint = float3(1.f);

        Camera* camera = World.SpawnActor<Camera>(m_Window.Width(), m_Window.Height());
        camera->GetTransform().Position = float3(0, 0, -10);
        m_Renderer.SetRenderingCamera(camera);

        DirectionalLight* light = World.SpawnActor<DirectionalLight>();
        const float4 Q0 = light->GetTransform().Rotation;
        HELIO_LOG_INFO("Light", "AFTER-SPAWN Q=({}, {}, {}, {})",
                       float(Q0.x), float(Q0.y), float(Q0.z), float(Q0.w));
        light->GetTransform().RotateEuler(0, 0, 0);

        const float4 Q1 = light->GetTransform().Rotation;
        HELIO_LOG_INFO("Light", "AFTER-ROTATE Q=({}, {}, {}, {})",
                       float(Q1.x), float(Q1.y), float(Q1.z), float(Q1.w));

        float time = 0;
        while (true)
        {
            HELIO_PROFILE_FRAME();
            HELIO_PROFILE_ZONE("Frame");
            const float Dt = static_cast<float>(m_EngineClock.Tick());

            m_Window.Dispatcher().BeginFrame();
            helio::debug::Tick(Dt); // decay debug-draw lifetimes — without this, lifetime-0 items live forever

            if (!m_Window.PumpEvents())
            {
                break;
            }

            m_Window.Dispatcher().FireHeld();

            World.Tick(Dt);
            time += Dt;

            Cuber->GetTransform().RotateAxis(float3(0, 1, 0), 3.f * Dt);
            SphereGuy->GetTransform().Position.y = (float)sin(time);

            CylinderDude->GetTransform().Rotation = QuatFromAxisAngle(float3(1, 0, 0), sin(time));
            // light->GetTransform().Rotation = QuatFromAxisAngle(float3(1, 0, 0), sin(time));

            m_Renderer.Render();
        }
        m_Renderer.WaitIdle();
    }
}
