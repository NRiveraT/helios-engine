#pragma once
#include "Actor.h"

namespace helio::gameplay
{
    class DirectionalLight final : public Actor
    {
    public:
        explicit DirectionalLight(HelioWorld& W);

        float3 LightColor = {1.f, 1.f, 1.f};
        float Intensity = 1.f;
    };
}