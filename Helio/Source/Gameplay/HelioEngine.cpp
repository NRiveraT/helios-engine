#include "HelioEngine.h"

#include <MeshPrimitives.h>
#include <RenderGraph.h>
#include <Logging/Log.h>
#include <Profile/Profile.h>
#include <Time/Clock.h>

#include "Actors/StaticMeshActor.h"
#include "Gameplay/World/HelioWorld.h"

using namespace helio;

namespace helio::gameplay
{
    void HelioEngine::Run()
    {
        HelioWorld World(*this);
        m_Renderer.SetWorld(World);

        StaticMeshActor* StaticMesh = World.SpawnActor<StaticMeshActor>(World);
        auto data = primitives::Cube(1.0f);
        StaticMesh->SetMesh(MeshSystem().CreateMesh({.Data = &data, .DebugName = "Cuber"}));

        core::Clock EngineClock;
        while (m_Window.PumpEvents())
        {
            HELIO_PROFILE_FRAME();
            HELIO_PROFILE_ZONE("Frame");

            const float Dt = static_cast<float>(EngineClock.Tick());
            World.Tick(Dt);

            StaticMesh->GetTransform().RotateAxis(float3(0, 1, 0), 3.f * Dt);
            
            m_Renderer.Render();
        }

        HELIO_PROFILE_ZONE("Shutdown");
        m_Renderer.WaitIdle();
    }
}
