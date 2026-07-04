#pragma once
#include "Actor.h"

namespace helio::gameplay
{
    class DirectionalLight final : public Actor
    {
    public:
        explicit DirectionalLight(HelioWorld& W);

        [[nodiscard]] float3 GetLightColor() const noexcept { return LightColor; }
        [[nodiscard]] float GetIntensity() const noexcept { return Intensity; }

        void SetLightColor(const float3& Color) noexcept { LightColor = Color; }
        void SetIntensity(float Intensity) noexcept { this->Intensity = Intensity; }

        float4x4 GetViewProjection() const noexcept;
        
        void OnRender() override;
        
        float3 LightColor = {1.f, 1.f, 1.f};
        float Intensity = 1.f;
    };
}