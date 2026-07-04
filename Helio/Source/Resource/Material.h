#pragma once
#include <Math/Math.h>

namespace helio::resource
{
    struct Material
    {
        float3 AlbedoTint{1.f};
        float Roughness{0.5f};
        float Metallic{0.f};
        float3 EmissiveColor{0.f};
        float EmissiveIntensity{0.f};
    };
}
