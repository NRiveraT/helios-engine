#include "MeshData.h"

#include <limits>

namespace helio::resource {

void MeshData::RecomputeBounds() {
    if (Vertices.empty()) {
        Bounds = {};
        return;
    }
    constexpr float Inf = std::numeric_limits<float>::infinity();
    float MinX =  Inf, MinY =  Inf, MinZ =  Inf;
    float MaxX = -Inf, MaxY = -Inf, MaxZ = -Inf;
    for (const auto& V : Vertices) {
        if (V.Pos[0] < MinX) MinX = V.Pos[0];
        if (V.Pos[1] < MinY) MinY = V.Pos[1];
        if (V.Pos[2] < MinZ) MinZ = V.Pos[2];
        if (V.Pos[0] > MaxX) MaxX = V.Pos[0];
        if (V.Pos[1] > MaxY) MaxY = V.Pos[1];
        if (V.Pos[2] > MaxZ) MaxZ = V.Pos[2];
    }
    Bounds.Min = float3(MinX, MinY, MinZ);
    Bounds.Max = float3(MaxX, MaxY, MaxZ);
}

} // namespace helio::resource
