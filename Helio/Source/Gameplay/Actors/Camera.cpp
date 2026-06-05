#include "Camera.h"

#include <ActionMap.h>
#include <algorithm>
#include <Logging/Log.h>

#include "World/HelioWorld.h"

namespace helio::gameplay
{
    Camera::Camera(HelioWorld& W, int ViewportWidth, int ViewportHeight) : Actor(W)
    {
        m_ViewportWidth = ViewportWidth;
        m_ViewportHeight = ViewportHeight;

        m_World->Engine().InputActionMap().BindKey("MoveLeft", input::Key::A);
        m_World->Engine().InputActionMap().BindKey("MoveRight", input::Key::D);
        m_World->Engine().InputActionMap().BindKey("MoveForward", input::Key::W);
        m_World->Engine().InputActionMap().BindKey("MoveBackward", input::Key::S);
        m_World->Engine().InputActionMap().BindKey("MoveUp", input::Key::E);
        m_World->Engine().InputActionMap().BindKey("MoveDown", input::Key::Q);
    }

    float4x4 Camera::GetViewProjection() const noexcept
    {
        return mul(m_Projection, m_LookAt);
    }

    void Camera::BeginPlay()
    {
        Actor::BeginPlay();
    }

    void Camera::Tick(float DeltaTime)
    {
        Actor::Tick(DeltaTime);

        auto& In = m_World->Engine().Window().Dispatcher();

        float2 MouseLook = float2(In.MouseDeltaX(), In.MouseDeltaY());

        float3 Move(0, 0, 0);
        if (In.IsActionHeld("MoveForward"))
        {
            Move += GetActorForwardVector();
        }
        if (In.IsActionHeld("MoveBackward"))
        {
            Move -= GetActorForwardVector();
        }
        if (In.IsActionHeld("MoveRight"))
        {
            Move += GetActorRightVector();
        }
        if (In.IsActionHeld("MoveLeft"))
        {
            Move -= GetActorRightVector();
        }
        if (In.IsActionHeld("MoveUp"))
        {
            Move += GetActorUpVector();
        }
        if (In.IsActionHeld("MoveDown"))
        {
            Move -= GetActorUpVector();
        }

        m_World->Engine().Window().SetMouseCaptured(In.IsMouseHeld(input::MouseButton::Right));
        
        if (m_World->Engine().Window().IsMouseCaptured())
        {
            m_Yaw += MouseLook.x * 0.001f;

            m_Pitch += MouseLook.y * 0.001f;
            m_Pitch = std::clamp(m_Pitch, -1.553f, 1.553f);

            const float4 QuatYaw = QuatFromAxisAngle(float3(0, 1, 0), m_Yaw);
            const float4 QuatPitch = QuatFromAxisAngle(float3(1, 0, 0), m_Pitch);

            GetTransform().Rotation = QuatMul(QuatYaw, QuatPitch);
        }

        float InputLen = (float)length(Move);
        if (InputLen > 0.1f)
        {
            Move = Move / InputLen;
            m_Velocity = Move * 2.f;
            m_Velocity = clamp(m_Velocity, float3(-3.f), float3(3.f));
            GetTransform().Translate(m_Velocity * DeltaTime);
        }
        else
        {
            m_Velocity = float3(0, 0, 0);
        }

        UpdateCameraMatrix();
    }

    void Camera::EndPlay()
    {
        Actor::EndPlay();
    }

    void Camera::UpdateCameraMatrix()
    {
        m_Projection = math::PerspectiveReverseZLH( hlslpp::radians(float1(45.0f)), static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight), 0.01f);
        m_LookAt = math::LookAtLH(GetTransform().Position, GetTransform().Position + GetActorForwardVector(), float3(0, 1, 0));
    }

    void Camera::MoveCamera(const float3& Direction)
    {
        // float3 Move = float3(0, 0, 0);
        // Move += Direction;
        //
        // const float Len = (float)length(Move);
        // if (Len > 0.0001f)
        // {
        //     Move = Move / Len;
        // }
        //
        // m_Input = Move;
        // m_Input = clamp(m_Input, float3(-1.f, -1, -1), float3(1, 1, 1));
    }
}
