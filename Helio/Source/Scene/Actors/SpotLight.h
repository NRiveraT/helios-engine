#pragma once
#include "LightActor.h"

namespace helio::scene
{
    class SpotLight final : public LightActor
    {
    public:
        explicit SpotLight(HelioWorld& W) : LightActor(W)
        {
            m_LightType = LightType::SpotLight;
        }

        [[nodiscard]] float GetSpotLightAngleMax() const noexcept { return m_AngleMax; }
        void SetSpotLightAngleMax(float AngleMax) noexcept { m_AngleMax = AngleMax; }

    private:
        float m_AngleMax = 5.f;
    };
}
