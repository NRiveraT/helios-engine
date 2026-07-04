#pragma once
#include <Mesh.h>
#include <Material.h>
#include <Math/Math.h>

namespace helio::core
{
    struct PendingInstance
    {
        resource::Mesh Mesh;
        resource::Material Material;
        float4x4 World; // already converted from Transform
    };
}
