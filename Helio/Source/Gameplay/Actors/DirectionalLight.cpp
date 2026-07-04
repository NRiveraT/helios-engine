#include "DirectionalLight.h"

namespace helio::gameplay
{
    DirectionalLight::DirectionalLight(HelioWorld& W) : Actor(W)
    {
    }

    float4x4 DirectionalLight::GetViewProjection() const noexcept
    {
        return float4x4::identity();
    }

    void DirectionalLight::OnRender()
    {
        Actor::OnRender();
    }
}
