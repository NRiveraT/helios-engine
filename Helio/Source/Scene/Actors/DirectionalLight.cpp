#include "DirectionalLight.h"

namespace helio::scene
{
    DirectionalLight::DirectionalLight(HelioWorld& W) : LightActor(W)
    {
        m_LightType = LightType::DirectionalLight;
    }
} // namespace helio::scene
