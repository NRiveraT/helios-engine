/// @file Camera.h
/// @brief Perspective camera actor — pure scene data, no input handling.
///
/// The camera's placement is its actor world transform; the view matrix is
/// that transform's exact rigid inverse (`Transform::ToViewMatrix`), so there
/// is exactly ONE source of truth for orientation. Movement/look controllers
/// live above the scene layer (see `gameplay::FlyCameraController`) and drive
/// the camera through the normal Actor transform API.
///
/// The projection is `math::PerspectiveReverseZLH` (reverse-Z, Y-flipped),
/// cached and rebuilt only when a parameter changes — never per frame.
#pragma once

#include <Scene/Actor.h>

namespace helio::scene
{
    class Camera : public Actor
    {
    public:
        Camera(HelioWorld& W, int ViewportWidth, int ViewportHeight);

        [[nodiscard]] const float4x4& GetProjection() const noexcept { return m_Projection; }
        [[nodiscard]] float4x4 GetView() const { return GetWorldTransform().ToViewMatrix(); }
        [[nodiscard]] float4x4 GetViewProjection() const
        {
            return hlslpp::mul(m_Projection, GetView());
        }

        void SetViewport(int Width, int Height);
        [[nodiscard]] int GetViewportWidth() const noexcept { return m_ViewportWidth; }
        [[nodiscard]] int GetViewportHeight() const noexcept { return m_ViewportHeight; }
        [[nodiscard]] float GetAspect() const noexcept
        {
            return static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
        }

        void SetFovY(float Radians);
        [[nodiscard]] float GetFovY() const noexcept { return m_FovY; }

        void SetNearZ(float NearZ);
        [[nodiscard]] float GetNearZ() const noexcept { return m_NearZ; }

    private:
        void RebuildProjection();

        float4x4 m_Projection;
        int m_ViewportWidth;
        int m_ViewportHeight;
        float m_FovY;
        float m_NearZ;
    };
} // namespace helio::scene
