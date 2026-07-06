#include "Camera.h"

namespace helio::scene
{
    Camera::Camera(HelioWorld& W, int ViewportWidth, int ViewportHeight)
        : Actor(W)
        , m_Projection(math::Identity())
        , m_ViewportWidth(ViewportWidth)
        , m_ViewportHeight(ViewportHeight)
        , m_FovY(90.0f * math::DegToRad)
        , m_NearZ(0.01f)
    {
        RebuildProjection();
    }

    void Camera::SetViewport(int Width, int Height)
    {
        if (Width == m_ViewportWidth && Height == m_ViewportHeight)
        {
            return;
        }
        m_ViewportWidth = Width;
        m_ViewportHeight = Height;
        RebuildProjection();
    }

    void Camera::SetFovY(float Radians)
    {
        m_FovY = Radians;
        RebuildProjection();
    }

    void Camera::SetNearZ(float NearZ)
    {
        m_NearZ = NearZ;
        RebuildProjection();
    }

    void Camera::RebuildProjection()
    {
        m_Projection = math::PerspectiveReverseZLH(m_FovY, GetAspect(), m_NearZ);
    }
} // namespace helio::scene
