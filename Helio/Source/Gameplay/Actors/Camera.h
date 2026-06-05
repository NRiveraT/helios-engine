#pragma once
#include "Actor.h"

namespace helio::gameplay
{
    class Camera : public Actor
    {
    public:
        explicit Camera(HelioWorld& W, int ViewportWidth, int ViewportHeight);

        [[nodiscard]] float4x4 GetViewProjection() const noexcept;
        
        [[nodiscard]] const float4x4& GetProjection() const noexcept { return m_Projection; }
        [[nodiscard]] const float4x4& GetView() const noexcept { return m_LookAt; }
        
        void BeginPlay() override;
        void Tick(float DeltaTime) override;
        void EndPlay() override;

    private:
        float4x4 m_Projection;
        float4x4 m_LookAt;

        int m_ViewportWidth;
        int m_ViewportHeight;
        
        float3 m_Velocity{0.f};
        float3 m_Input{0.f};

        float m_Pitch = 0.f;
        float m_Yaw = 0.f;
        float m_Zoom = 0.f;

        void UpdateCameraMatrix();
        void MoveCamera(const float3& Direction);
    };
}
