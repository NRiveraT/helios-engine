#include "EditorOverlay.h"

#include <Core/Logging/Log.h>
#include <Core/Math/Transform.h>

#include <Platform/Windows/Window.h>

#include <Scene/Actors/Camera.h>
#include <Scene/Actors/DirectionalLight.h>
#include <Scene/Actors/StaticMeshActor.h>
#include <Scene/HelioWorld.h>
#include <Scene/SceneRenderer.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h> // IWYU pragma: keep
#include <imgui_internal.h>  // DockBuilder API (docking branch)

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>

namespace helio::editor
{
    EditorOverlay::EditorOverlay(platform::windows::Window& Win, rhi::Device& Dev, scene::SceneRenderer& Renderer, rhi::Format TargetFormat)
        : m_Window(&Win)
          , m_Dev(&Dev)
          , m_SceneRenderer(&Renderer)
          , m_Ctx((IMGUI_CHECKVERSION(), ImGui::CreateContext()))
          , m_Renderer(Dev, TargetFormat) // needs the context: builds the font atlas
    {
        ImGuiIO& IO = ImGui::GetIO();
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // NOTE: intentionally NOT enabling ImGuiConfigFlags_NavEnableKeyboard.
        // Keyboard nav makes ImGui claim WantCaptureKeyboard whenever a panel
        // is focused, which would swallow WASD and make the fly camera
        // impossible while the editor is open.
        IO.IniFilename = "EditorLayout.ini"; // persist dock layout next to the binary
        // First run (no saved layout): build the default dock arrangement.
        m_BuildDefaultLayout = !std::filesystem::exists(IO.IniFilename);
        ImGui::StyleColorsDark();

        ImGui_ImplSDL3_InitForVulkan(Win.Native());

        Win.SetNativeEventHook([this](const void* E) { return HandleNativeEvent(E); });

        HELIO_LOG_INFO("Editor", "Editor overlay ready — press F1 to toggle.");
    }

    EditorOverlay::~EditorOverlay()
    {
        m_Window->SetNativeEventHook({});
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(m_Ctx);
    }

    void EditorOverlay::SetVisible(bool Visible)
    {
        if (m_Visible == Visible)
        {
            return;
        }
        m_Visible = Visible;
        // Whichever way we toggle, the set of event consumers just changed —
        // drop held-key state so gameplay doesn't chase releases it never saw,
        // and release the fly-cam mouse capture.
        m_Window->Dispatcher().ResetHeldState();
        m_Window->SetMouseCaptured(false);
    }

    bool EditorOverlay::WantsInput() const
    {
        if (!m_Visible)
        {
            return false;
        }
        // Gate the fly camera on the MOUSE only: the camera is a hold-RMB
        // control, so it should be suppressed exactly when the cursor is over
        // an editor panel (WantCaptureMouse), and left free over the 3D
        // viewport. Keyboard capture is deliberately excluded — otherwise a
        // focused panel would disable the camera even while you fly the
        // viewport with RMB held.
        const ImGuiIO& IO = ImGui::GetIO();
        return IO.WantCaptureMouse;
    }

    bool EditorOverlay::HandleNativeEvent(const void* NativeEvent)
    {
        const SDL_Event* Ev = static_cast<const SDL_Event*>(NativeEvent);

        // Editor toggle is handled here (not via the action map) so it works
        // regardless of what gameplay bound, and key-repeat is filtered.
        if (Ev->type == SDL_EVENT_KEY_DOWN && Ev->key.key == SDLK_F1 && !Ev->key.repeat)
        {
            Toggle();
            return true;
        }

        // Only feed ImGui while visible. Feeding it while hidden (to "keep
        // state warm") accumulates events forever in ImGui's input queue,
        // because the queue is only drained inside ImGui::NewFrame — which we
        // skip while hidden. That is an unbounded memory leak plus a backlog
        // that replays on the next toggle.
        if (!m_Visible)
        {
            return false;
        }

        ImGui_ImplSDL3_ProcessEvent(Ev);

        const ImGuiIO& IO = ImGui::GetIO();
        switch (Ev->type)
        {
        // RELEASES are never consumed: a key/button whose release ImGui
        // swallowed would stay "held" forever in the gameplay dispatcher
        // (stuck-key bug). Presses/motion are gated on ImGui's capture.
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return false;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_TEXT_INPUT:
            return IO.WantCaptureKeyboard;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
            return IO.WantCaptureMouse;
        default:
            return false;
        }
    }

    void EditorOverlay::BeginFrame()
    {
        // If the previous frame opened an ImGui frame that never got rendered
        // — SceneRenderer::Render drops the frame when the swapchain is out of
        // date (resize/minimize), so the overlay hook (our ImGui::Render) never
        // runs — close it now. Otherwise the next NewFrame trips ImGui's
        // "forgot to call Render/EndFrame" assert and hard-crashes in debug.
        if (m_FrameActive)
        {
            ImGui::EndFrame();
            m_FrameActive = false;
        }

        if (!m_Visible)
        {
            return;
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        DrawDockspace();
        DrawScenePanel();
        DrawInspectorPanel();
        DrawStatsPanel();

        m_FrameActive = true;
    }

    void EditorOverlay::Render(render::RenderGraph& Rg, rhi::TextureHandle Target,
                               uint32_t Width, uint32_t Height)
    {
        if (!m_FrameActive)
        {
            return;
        }
        ImGui::Render();
        m_Renderer.Render(Rg, Target, ImGui::GetDrawData(), Width, Height);
        m_FrameActive = false;
    }

    // ---- Panels ---------------------------------------------------------------

    void EditorOverlay::DrawDockspace()
    {
        // Fullscreen dockspace with a passthrough central node: the 3D scene
        // shows through the middle; panels dock around the edges.
        const ImGuiID DockspaceId = ImGui::DockSpaceOverViewport(
            0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (m_BuildDefaultLayout)
        {
            m_BuildDefaultLayout = false;
            ImGui::DockBuilderRemoveNode(DockspaceId);
            ImGui::DockBuilderAddNode(DockspaceId,
                                      ImGuiDockNodeFlags_DockSpace |
                                      ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::DockBuilderSetNodeSize(DockspaceId, ImGui::GetMainViewport()->WorkSize);

            ImGuiID Center = DockspaceId;
            ImGuiID Left = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Left, 0.20f, nullptr, &Center);
            ImGuiID Right = ImGui::DockBuilderSplitNode(Center, ImGuiDir_Right, 0.25f, nullptr, &Center);
            ImGuiID LeftBottom = ImGui::DockBuilderSplitNode(Left, ImGuiDir_Down, 0.30f, nullptr, &Left);

            ImGui::DockBuilderDockWindow("Scene", Left);
            ImGui::DockBuilderDockWindow("Stats", LeftBottom);
            ImGui::DockBuilderDockWindow("Inspector", Right);
            ImGui::DockBuilderFinish(DockspaceId);
        }
    }

    scene::Actor* EditorOverlay::ResolveSelected() const
    {
        if (m_World == nullptr || m_SelectedId == 0)
        {
            return nullptr;
        }
        for (const auto& A : m_World->GetActors())
        {
            if (A && A->Id() == m_SelectedId && !A->IsPendingDestroy())
            {
                return A.get();
            }
        }
        return nullptr;
    }

    void EditorOverlay::DrawSceneNode(scene::Actor& A)
    {
        ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_DefaultOpen;
        if (A.GetChildren().empty())
        {
            Flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (A.Id() == m_SelectedId)
        {
            Flags |= ImGuiTreeNodeFlags_Selected;
        }

        const char* Label = A.GetName().empty() ? "(unnamed)" : A.GetName().c_str();
        const bool Open = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(static_cast<uintptr_t>(A.Id())), Flags, "%s", Label);

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        {
            m_SelectedId = A.Id();
        }

        if (ImGui::BeginPopupContextItem())
        {
            m_SelectedId = A.Id();
            if (ImGui::MenuItem("Delete (with children)"))
            {
                m_World->DestroyActor(A);
                m_SelectedId = 0;
            }
            if (A.GetParent() != nullptr && ImGui::MenuItem("Detach from parent"))
            {
                A.Detach();
            }
            ImGui::EndPopup();
        }

        if (Open)
        {
            // Copy: Detach/Delete inside children mutates the list mid-walk.
            const std::vector<scene::Actor*> Children = A.GetChildren();
            for (scene::Actor* Child : Children)
            {
                if (Child != nullptr && !Child->IsPendingDestroy())
                {
                    DrawSceneNode(*Child);
                }
            }
            ImGui::TreePop();
        }
    }

    void EditorOverlay::DrawScenePanel()
    {
        if (!ImGui::Begin("Scene"))
        {
            ImGui::End();
            return;
        }

        if (m_World == nullptr)
        {
            ImGui::TextDisabled("No world bound.");
            ImGui::End();
            return;
        }

        for (const auto& A : m_World->GetActors())
        {
            if (A && A->GetParent() == nullptr && !A->IsPendingDestroy())
            {
                DrawSceneNode(*A);
            }
        }

        ImGui::End();
    }

    void EditorOverlay::DrawInspectorPanel()
    {
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }

        scene::Actor* Selected = ResolveSelected();
        if (Selected == nullptr)
        {
            ImGui::TextDisabled("Select an actor in the Scene panel.");
            ImGui::End();
            return;
        }

        // ---- Name ------------------------------------------------------------
        char NameBuf[128];
        std::strncpy(NameBuf, Selected->GetName().c_str(), sizeof(NameBuf) - 1);
        NameBuf[sizeof(NameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", NameBuf, sizeof(NameBuf)))
        {
            Selected->SetName(NameBuf);
        }
        ImGui::Separator();

        // ---- Local transform ---------------------------------------------------
        const Transform& Local = Selected->GetLocalTransform();

        float Pos[3] = {float(Local.Position.x), float(Local.Position.y), float(Local.Position.z)};
        if (ImGui::DragFloat3("Position", Pos, 0.05f))
        {
            Selected->SetLocalPosition(float3(Pos[0], Pos[1], Pos[2]));
        }

        // Rotation: edit cached Euler degrees while the widget is active so
        // quat->Euler->quat round trips can't jitter mid-drag.
        if (!m_EulerEditing || m_EulerCacheId != Selected->Id())
        {
            const float3 Euler = QuatToEuler(Local.Rotation);
            m_EulerCache[0] = float(Euler.x) * math::RadToDeg;
            m_EulerCache[1] = float(Euler.y) * math::RadToDeg;
            m_EulerCache[2] = float(Euler.z) * math::RadToDeg;
            m_EulerCacheId = Selected->Id();
        }
        if (ImGui::DragFloat3("Rotation (P/Y/R)", m_EulerCache, 0.5f))
        {
            Selected->SetLocalRotation(QuatFromEuler(m_EulerCache[0] * math::DegToRad, m_EulerCache[1] * math::DegToRad, m_EulerCache[2] * math::DegToRad));
        }
        m_EulerEditing = ImGui::IsItemActive();

        float Scale[3] = {float(Local.Scale.x), float(Local.Scale.y), float(Local.Scale.z)};
        if (ImGui::DragFloat3("Scale", Scale, 0.02f, 0.001f, 1000.0f))
        {
            Selected->SetLocalScale(float3(Scale[0], Scale[1], Scale[2]));
        }

        // ---- Type-specific sections ---------------------------------------------
        if (auto* MeshActor = dynamic_cast<scene::StaticMeshActor*>(Selected))
        {
            // Reference, not a copy — editing a copy would discard the change.
            auto& Sections = MeshActor->GetMeshSections();
            ImGui::SeparatorText("Mesh Sections");
            ImGui::Text("%zu section(s)", Sections.size());

            for (size_t S = 0; S < Sections.size(); ++S)
            {
                // Per-section id so identical widget labels across sections
                // don't collide in ImGui's id stack.
                ImGui::PushID(static_cast<int>(S));
                char Header[32];
                std::snprintf(Header, sizeof(Header), "Section %zu", Sections[S].SectionName);
                if (ImGui::CollapsingHeader(Header, S == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                {
                    resource::Material& Mat = Sections[S].Material;

                    float Albedo[3] = {float(Mat.AlbedoTint.x), float(Mat.AlbedoTint.y), float(Mat.AlbedoTint.z)};
                    if (ImGui::ColorEdit3("Albedo", Albedo))
                    {
                        Mat.AlbedoTint = float3(Albedo[0], Albedo[1], Albedo[2]);
                    }
                    ImGui::SliderFloat("Roughness", &Mat.Roughness, 0.0f, 1.0f);
                    ImGui::SliderFloat("Metallic", &Mat.Metallic, 0.0f, 1.0f);

                    float Emissive[3] = {float(Mat.EmissiveColor.x), float(Mat.EmissiveColor.y), float(Mat.EmissiveColor.z)};
                    if (ImGui::ColorEdit3("Emissive", Emissive))
                    {
                        Mat.EmissiveColor = float3(Emissive[0], Emissive[1], Emissive[2]);
                    }
                    ImGui::DragFloat("Emissive Intensity", &Mat.EmissiveIntensity, 0.05f, 0.0f, 100.0f);

                    // Live texture thumbnails — the ImGui backend uses a
                    // texture's bindless SampledSlot as its ImTextureID, so any
                    // loaded material texture displays for free.
                    const auto Thumb = [](const char* Label, uint32_t Slot)
                    {
                        if (Slot == resource::kNoTexture) return;
                        ImGui::Image(static_cast<ImTextureID>(Slot), ImVec2(48, 48));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (slot %u)", Label, Slot);
                        ImGui::SameLine();
                    };
                    Thumb("Albedo", Mat.AlbedoTex);
                    Thumb("Normal", Mat.NormalTex);
                    Thumb("MetalRough", Mat.MetalRoughTex);
                    Thumb("Emissive", Mat.EmissiveTex);
                    Thumb("Occlusion", Mat.OcclusionTex);
                    ImGui::NewLine();

                    ImGui::TextDisabled("%u tris", Sections[S].Mesh.Stats.TriangleCount);
                }
                ImGui::PopID();
            }
        }

        if (auto* Light = dynamic_cast<scene::DirectionalLight*>(Selected))
        {
            ImGui::SeparatorText("Directional Light");
            float Color[3] = {float(Light->GetColor().x), float(Light->GetColor().y), float(Light->GetColor().z)};
            if (ImGui::ColorEdit3("Color", Color))
            {
                Light->SetColor(float3(Color[0], Color[1], Color[2]));
            }
            float Intensity = Light->GetIntensity();
            if (ImGui::DragFloat("Intensity", &Intensity, 0.02f, 0.0f, 100.0f))
            {
                Light->SetIntensity(Intensity);
            }
            float Ambient = Light->GetAmbient();
            if (ImGui::SliderFloat("Ambient", &Ambient, 0.0f, 1.0f))
            {
                Light->SetAmbient(Ambient);
            }
        }

        if (auto* Cam = dynamic_cast<scene::Camera*>(Selected))
        {
            ImGui::SeparatorText("Camera");
            float FovDeg = Cam->GetFovY() * math::RadToDeg;
            if (ImGui::SliderFloat("FOV (deg)", &FovDeg, 10.0f, 140.0f))
            {
                Cam->SetFovY(FovDeg * math::DegToRad);
            }
            float NearZ = Cam->GetNearZ();
            if (ImGui::DragFloat("Near Z", &NearZ, 0.001f, 0.0001f, 10.0f, "%.4f"))
            {
                Cam->SetNearZ(NearZ);
            }
        }

        ImGui::End();
    }

    void EditorOverlay::DrawStatsPanel()
    {
        if (!ImGui::Begin("Stats"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
        ImGui::Text("CPU render  %.2f ms", m_SceneRenderer->LastRenderCpuMs());
        ImGui::Text("GPU frame   %.2f ms", m_Dev->LastFrameGpuMs());
        if (m_World != nullptr)
        {
            ImGui::Text("Actors      %zu", m_World->GetActors().size());
        }
        ImGui::Separator();

        const char* DebugViewMode[3] = {"Lit", "Depth", "WorldNormal"};

        if (ImGui::BeginCombo("Debug View Mode", DebugViewMode[m_SceneRenderer->GetDebugViewMode()]))
        {
            for (int n = 0, n_end = IM_ARRAYSIZE(DebugViewMode); n < n_end; n++)
            {
                const bool is_selected = (m_SceneRenderer->GetDebugViewMode() == n);
                if (ImGui::Selectable(DebugViewMode[n], is_selected))
                {
                    m_SceneRenderer->SetDebugViewMode(n);
                }
                
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("F1 toggles editor. Hold RMB in the viewport to fly (WASD/EQ).");

        ImGui::End();
    }
} // namespace helio::editor
