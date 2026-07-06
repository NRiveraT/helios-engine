#pragma once
#include <Math/Math.h>

#include <cstdint>

namespace helio::resource
{
    /// Sentinel for "this material slot has no texture" — the shader falls back
    /// to the factor alone. Matches `kNoTexture` in Shaders/Passes/MeshInstanced.slang.
    inline constexpr uint32_t kNoTexture = 0xFFFFFFFFu;

    struct Material
    {
        // ---- Factors (glTF metallic-roughness) -----------------------------
        float3 AlbedoTint{1.f};
        float Roughness{0.5f};
        float Metallic{0.f};
        float3 EmissiveColor{0.f};
        float EmissiveIntensity{0.f};

        // ---- Texture slots (bindless SampledSlots; kNoTexture = none) -------
        // Each multiplies/replaces the matching factor per the glTF spec.
        // AlbedoTex + EmissiveTex are sRGB; the rest are linear data textures.
        uint32_t AlbedoTex{kNoTexture};      ///< base color (sRGB), × AlbedoTint
        uint32_t NormalTex{kNoTexture};      ///< tangent-space normal map (linear)
        uint32_t MetalRoughTex{kNoTexture};  ///< linear; G=roughness, B=metallic (glTF packing)
        uint32_t EmissiveTex{kNoTexture};    ///< emissive (sRGB), × EmissiveColor
        uint32_t OcclusionTex{kNoTexture};   ///< linear; R=ambient occlusion

        /// Exact (bitwise-value) equality — the renderer uses this to decide
        /// which instances can share one instanced draw call. Two actors with
        /// identical materials batch together; differing materials (incl.
        /// different textures) split into separate draws so each is honored.
        [[nodiscard]] bool operator==(const Material& O) const noexcept
        {
            return hlslpp::all(AlbedoTint == O.AlbedoTint) &&
                   Roughness == O.Roughness &&
                   Metallic == O.Metallic &&
                   hlslpp::all(EmissiveColor == O.EmissiveColor) &&
                   EmissiveIntensity == O.EmissiveIntensity &&
                   AlbedoTex == O.AlbedoTex &&
                   NormalTex == O.NormalTex &&
                   MetalRoughTex == O.MetalRoughTex &&
                   EmissiveTex == O.EmissiveTex &&
                   OcclusionTex == O.OcclusionTex;
        }
        [[nodiscard]] bool operator!=(const Material& O) const noexcept { return !(*this == O); }
    };
}
