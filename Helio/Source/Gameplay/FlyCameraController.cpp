#include "FlyCameraController.h"

#include <Core/Math/Transform.h>
#include <Input/ActionMap.h>
#include <Input/Dispatcher.h>
#include <Input/Keys.h>
#include <Platform/Windows/Window.h>
#include <Scene/Actors/Camera.h>

#include <algorithm>
#include <cmath>

namespace helio::gameplay
{
    void FlyCameraController::RetargetCamera(scene::Camera* Cam)
    {
        if (Cam == nullptr)
        {
            m_Camera = nullptr;
            m_CameraId = 0;
            return;
        }
        // Only re-sync the look angles when the camera identity changes —
        // re-pointing at the same camera every frame must be a no-op or it
        // would fight the accumulated yaw/pitch.
        if (Cam->Id() != m_CameraId)
        {
            const float3 Euler = QuatToEuler(Cam->GetWorldRotation());
            m_Pitch = float(Euler.x);
            m_Yaw = float(Euler.y);
            m_CameraId = Cam->Id();
        }
        m_Camera = Cam;
    }

    void FlyCameraController::BindInput(input::ActionMap& Map) const
    {
        Map.BindKey("Camera.Left", input::Key::A);
        Map.BindKey("Camera.Right", input::Key::D);
        Map.BindKey("Camera.Forward", input::Key::W);
        Map.BindKey("Camera.Backward", input::Key::S);
        Map.BindKey("Camera.Up", input::Key::E);
        Map.BindKey("Camera.Down", input::Key::Q);
    }

    void FlyCameraController::Update(platform::windows::Window& Win, float DeltaTime)
    {
        if (m_Camera == nullptr)
        {
            return;
        }
        if (!m_Enabled)
        {
            Win.SetMouseCaptured(false);
            return;
        }

        auto& In = Win.Dispatcher();

        // Hold right mouse to fly — the scene-viewport convention in Unreal and
        // Unity. Both look AND WASD/EQ movement engage only while RMB is held,
        // so with the editor open you type into panels normally and fly the
        // moment you grab the viewport with RMB. Releasing RMB hands the
        // keyboard straight back to the UI.
        const bool Flying = In.IsMouseHeld(input::MouseButton::Right);
        Win.SetMouseCaptured(Flying);
        if (!Flying)
        {
            return;
        }

        m_Yaw += In.MouseDeltaX() * m_LookSensitivity;
        m_Pitch += In.MouseDeltaY() * m_LookSensitivity; // mouse down = look down
        // Keep just shy of the poles so forward never degenerates.
        constexpr float kPitchLimit = math::HalfPi - 0.01f;
        m_Pitch = std::clamp(m_Pitch, -kPitchLimit, kPitchLimit);
        m_Camera->SetWorldRotation(QuatFromEuler(m_Pitch, m_Yaw, 0.0f));

        float3 Move(0.0f, 0.0f, 0.0f);
        if (In.IsActionHeld("Camera.Forward")) Move += m_Camera->GetForward();
        if (In.IsActionHeld("Camera.Backward")) Move -= m_Camera->GetForward();
        if (In.IsActionHeld("Camera.Right")) Move += m_Camera->GetRight();
        if (In.IsActionHeld("Camera.Left")) Move -= m_Camera->GetRight();
        if (In.IsActionHeld("Camera.Up")) Move += m_Camera->GetUp();
        if (In.IsActionHeld("Camera.Down")) Move -= m_Camera->GetUp();

        const float Len = std::sqrt(float(hlslpp::dot(Move, Move)));
        if (Len > 1e-4f)
        {
            m_Camera->AddWorldOffset(Move * (m_MoveSpeed * DeltaTime / Len));
        }
    }
} // namespace helio::gameplay
