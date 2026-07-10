#pragma once
#include <Actor.h>

namespace helio::scene
{
    enum class LightType : uint8_t
    {
        DirectionalLight,
        PointLight,
        SpotLight,
        NONE
    };

    class LightActor : public Actor
    {
    public:
        explicit LightActor(HelioWorld& W) : Actor(W)
        {
        }

        [[nodiscard]] float3 GetColor() const noexcept { return m_Color; }
        void SetColor(float3 Color) noexcept { m_Color = Color; }

        [[nodiscard]] float GetIntensity() const noexcept { return m_Intensity; }
        void SetIntensity(float Intensity) noexcept { m_Intensity = Intensity; }

        [[nodiscard]] LightType GetLightType() const noexcept { return m_LightType; }

        [[nodiscard]] float GetRange() const noexcept { return m_Range; }
        void SetRange(float Range) noexcept { m_Range = Range; }

    protected:
        LightType m_LightType = LightType::NONE;

    private:
        float3 m_Color{1.0f, 1.0f, 1.0f};
        float m_Range = 5.0f;
        float m_Intensity = 10.0f;
    };
}
