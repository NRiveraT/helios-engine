#include "HelioEngine.h"

#include <Core/Profile/Profile.h>

#include <Input/Event.h>
#include <Renderer/Debug/DebugDraw.h>
#include <Resource/MeshPrimitives.h>

#include <Scene/Actors/Camera.h>
#include <Scene/Actors/DirectionalLight.h>
#include <Scene/Actors/StaticMeshActor.h>
#include <Scene/HelioWorld.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace helio::gameplay
{
    void HelioEngine::Run()
    {
        HELIO_PROFILE_ZONE("Startup");

        Window().Dispatcher().SetActionMap(&m_InputActionMap);

        scene::HelioWorld World;
        m_Renderer.SetWorld(World);
        m_Editor.SetWorld(&World);
        m_Renderer.SetOverlayHook([this](render::RenderGraph& Rg, rhi::TextureHandle Target, uint32_t W, uint32_t H) { m_Editor.Render(Rg, Target, W, H); });

        // ---- Test scene ----------------------------------------------------
        using scene::StaticMeshActor;

        // auto SphereData = resource::primitives::Sphere(0.5f, 32, 16);
        // StaticMeshActor* Sphere = World.SpawnActorNamed<StaticMeshActor>("Sphere", MeshSystem().CreateMesh({.Data = &SphereData, .DebugName = "Sphere"}));
        // Sphere->SetLocalPosition(float3(-2.0f, 0.0f, 0.0f));
        // Sphere->GetMaterial().AlbedoTint = float3(1.0f, 0.0f, 0.0f);
        // Sphere->GetMaterial().Roughness = 0.2f;
        //
        // // Hierarchy demo: a small moon parented to the sphere. It inherits the
        // // sphere's bobbing + spin through the scene graph — zero code below.
        // auto MoonData = resource::primitives::Sphere(0.15f, 16, 8);
        // StaticMeshActor* Moon = World.SpawnActorNamed<StaticMeshActor>("Sphere.Moon", MeshSystem().CreateMesh({.Data = &MoonData, .DebugName = "Moon"}));
        // Moon->AttachTo(*Sphere, scene::Actor::AttachRule::KeepLocal);
        // Moon->SetLocalPosition(float3(0.9f, 0.35f, 0.0f));
        // Moon->GetMaterial().AlbedoTint = float3(0.9f, 0.9f, 0.2f);
        // Moon->GetMaterial().Roughness = 0.6f;
        //
        // auto CylinderData = resource::primitives::Cylinder(0.5f, 1, 16);
        // StaticMeshActor* Cylinder = World.SpawnActorNamed<StaticMeshActor>("Cylinder", MeshSystem().CreateMesh({.Data = &CylinderData, .DebugName = "Cylinder"}));
        // Cylinder->SetLocalPosition(float3(2.0f, 0.0f, 0.0f));
        // Cylinder->GetMaterial().AlbedoTint = float3(0.0f, 1.0f, 0.0f);
        // Cylinder->GetMaterial().Roughness = 0.6f;
        //
        // auto PlaneData = resource::primitives::Plane(10.0f, 10.0f);
        // StaticMeshActor* Ground = World.SpawnActorNamed<StaticMeshActor>("Ground", MeshSystem().CreateMesh({.Data = &PlaneData, .DebugName = "Ground"}));
        // Ground->SetLocalPosition(float3(0.0f, -1.0f, 0.0f));
        // Ground->GetMaterial().AlbedoTint = float3(1.0f);
        // Import a whole glTF model as one actor: LoadModel returns a section
        // (mesh + its material, resolved from the glTF material at import) per
        // primitive, and SetSections hands the lot to the actor in one call —
        // set it and forget it.
        StaticMeshActor* MeshActor = World.SpawnActorNamed<StaticMeshActor>("Helmet");
        MeshActor->SetSections(MeshSystem().LoadModel("Assets/DamagedHelmet.glb"));
        // Face the visor toward the camera's default view (it points +Z after
        // the RH->LH import flip; a 180° yaw turns it around).
        MeshActor->SetLocalRotation(QuatFromAxisAngle(float3(0.0f, 1.0f, 0.0f), math::Pi));

        // Frame the camera on the imported model's combined bounds so we're not
        // guessing at its scale/placement.
        math::AABB ModelBounds;
        for (const auto& Section : MeshActor->GetMeshSections())
        {
            if (Section.Mesh.Bounds.IsValid())
            {
                ModelBounds.Expand(Section.Mesh.Bounds);
            }
        }

        StaticMeshActor* Sponza = World.SpawnActorNamed<StaticMeshActor>("Sponza");
        Sponza->SetSections(MeshSystem().LoadModel("Assets/Sponza/Sponza.glb"));

        scene::Camera* Camera = World.SpawnActorNamed<scene::Camera>("Camera", m_Window.Width(), m_Window.Height());
        if (ModelBounds.IsValid())
        {
            // Stand back from a compact object by a couple of its radii, along
            // the camera's default forward (+Z), so the whole thing frames.
            // NearZ scales with the model so precision holds at any size.
            const float3 Center = ModelBounds.Center();
            const float3 Ext = ModelBounds.Extents();
            const float Radius = std::max(std::sqrt(float(hlslpp::dot(Ext, Ext))), 0.1f);
            Camera->SetWorldPosition(Center - float3(0.0f, 0.0f, Radius * 2.4f));
            Camera->SetNearZ(std::max(Radius * 0.01f, 0.01f));
        }
        else
        {
            Camera->SetWorldPosition(float3(0.0f, 0.0f, -10.0f));
        }
        m_CameraController.BindInput(m_InputActionMap);

        scene::DirectionalLight* Sun = World.SpawnActorNamed<scene::DirectionalLight>("Sun");
        // Aim the sun down at ~50° with a bit of yaw. NOTE: SetWorldRotation
        // takes a QUATERNION — build it from Euler angles with QuatFromEuler
        // (pitch, yaw, roll in radians). Passing float4(euler...) directly is
        // not a valid rotation. Interior scenes (Sponza) need a strong sun +
        // healthy ambient to read, since most surfaces face away from the sun.
        Sun->SetWorldRotation(QuatFromEuler(50.0f * math::DegToRad, 30.0f * math::DegToRad, 0.0f));
        Sun->SetIntensity(4.0f);
        Sun->SetAmbient(0.25f);

        // Actors referenced across frames are held by stable id, never by raw
        // pointer — the editor can destroy any actor, and a dangling raw
        // pointer would be a use-after-free the next frame. Resolve each id
        // through the world (which filters pending-destroy) right before use.
        const uint64_t CameraId = Camera->Id();
        // const uint64_t CubeId = Cube->Id();
        // const uint64_t SphereId = Sphere->Id();
        // const uint64_t CylinderId = Cylinder->Id();

        // Coalesced window resize: SDL fires many WINDOW_RESIZED events during
        // a drag; we record the latest size and apply it once per frame,
        // before rendering, on the same thread that owns the GPU resources.
        int PendingResizeW = 0, PendingResizeH = 0;
        m_Window.Dispatcher().OnEvent([&](const input::InputEvent& E)
        {
            if (E.Type == input::InputEvent::Kind::WindowResize)
            {
                PendingResizeW = E.ResizeEv.Width;
                PendingResizeH = E.ResizeEv.Height;
            }
        });

        // ---- Main loop --------------------------------------------------------
        float Time = 0.0f;
        while (true)
        {
            HELIO_PROFILE_FRAME();
            HELIO_PROFILE_ZONE("Frame");
            const float Dt = static_cast<float>(m_EngineClock.Tick());

            m_Window.Dispatcher().BeginFrame();
            helio::debug::Tick(Dt); // decay debug-draw lifetimes

            if (!m_Window.PumpEvents())
            {
                break;
            }

            // Apply a pending resize before touching the GPU this frame. Skip
            // zero sizes (minimize) — the offscreen targets keep their last
            // valid extent and rendering resumes on restore.
            if (PendingResizeW > 0 && PendingResizeH > 0 && (PendingResizeW != m_Renderer.GetWidth() || PendingResizeH != m_Renderer.GetHeight()))
            {
                m_RHI.WaitIdle();
                m_RHI.Resize(PendingResizeW, PendingResizeH);
                m_Renderer.Resize(PendingResizeW, PendingResizeH);
                if (auto* Cam = static_cast<scene::Camera*>(World.FindActorById(CameraId)))
                {
                    Cam->SetViewport(PendingResizeW, PendingResizeH);
                }
            }

            m_Window.Dispatcher().FireHeld();

            // Build the editor UI first — its capture state then gates the
            // fly camera (clicks on editor windows must not fly the camera).
            m_Editor.BeginFrame();
            m_CameraController.SetEnabled(!m_Editor.WantsInput());

            World.Tick(Dt); // may flush editor-requested actor deletions
            Time += Dt;

            // Re-resolve the camera AFTER Tick (an editor delete this frame is
            // now flushed) and hand the fresh pointer to both consumers; either
            // may be null and both handle it.
            auto* Cam = static_cast<scene::Camera*>(World.FindActorById(CameraId));
            m_Renderer.SetRenderingCamera(Cam);
            m_CameraController.RetargetCamera(Cam);
            m_CameraController.Update(m_Window, Dt);

            // Scene animation — exercises world rotation, local position, and
            // (through the moon) hierarchical transform propagation. Every
            // actor is re-resolved by id and null-checked, so deleting any of
            // them from the editor is safe.
            // if (auto* A = static_cast<scene::StaticMeshActor*>(World.FindActorById(CubeId)))
            //     A->AddWorldRotation(float3(0.0f, 1.0f, 0.0f), -0.5f * Dt);
            // if (auto* A = static_cast<scene::StaticMeshActor*>(World.FindActorById(SphereId)))
            // {
            //     A->SetLocalPosition(float3(-2.0f, std::sin(Time), 0.0f));
            //     A->AddWorldRotation(float3(0.0f, 1.0f, 0.0f), 1.5f * Dt);
            // }
            // if (auto* A = static_cast<scene::StaticMeshActor*>(World.FindActorById(CylinderId)))
            //     A->SetLocalRotation(QuatFromAxisAngle(float3(1.0f, 0.0f, 0.0f), std::sin(Time)));

            m_Renderer.Render();
        }
        m_Renderer.WaitIdle();
        m_Editor.SetWorld(nullptr); // World dies with this scope
        m_Renderer.SetRenderingCamera(nullptr);
        m_Renderer.SetOverlayHook({});
    }
} // namespace helio::gameplay
