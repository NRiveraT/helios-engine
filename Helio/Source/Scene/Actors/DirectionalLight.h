/// @file DirectionalLight.h
/// @brief Sun-style directional light actor.
///
/// The light's DIRECTION is its actor world +Z (forward) axis — rotate the
/// actor to aim it; there is no separate direction field to fall out of sync.
/// The SceneRenderer picks the first DirectionalLight in the world each frame,
/// derives the shadow view/projection by fitting the caster bounds (see
/// `SceneRenderer::BuildShadowMatrix`), and feeds direction/color/intensity
/// to the shaders through the per-frame constants buffer.
#pragma once

#include <Scene/Actor.h>

#include <Actors/LightActor.h>

namespace helio::scene
{
    class DirectionalLight final : public LightActor
    {
    public:
        explicit DirectionalLight(HelioWorld& W);

        /// Flat ambient term applied scene-wide (V1: carried by the sun until
        /// an environment-lighting system exists). Keeps unlit faces readable.
        [[nodiscard]] float GetAmbient() const noexcept { return m_Ambient; }
        void SetAmbient(float Ambient) noexcept { m_Ambient = Ambient; }

        /// World-space direction the light TRAVELS (from the sun toward the
        /// scene) — the actor's forward axis.
        [[nodiscard]] float3 GetDirection() const { return GetForward(); }

    private:
        float m_Ambient = 0.0f;
    };
} // namespace helio::scene
