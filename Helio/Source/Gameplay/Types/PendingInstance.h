#pragma once
#include <Mesh.h>
#include <Math/Math.h>

namespace helio::gameplay
{
    struct PendingInstance
    {
        resource::Mesh Mesh;
        float4x4 World; // already converted from Transform
    };
}
