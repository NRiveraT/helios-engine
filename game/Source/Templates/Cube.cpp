#include "Cube.h"
#include <MeshPrimitives.h>

Cube::Cube(HelioWorld& W): StaticMeshActor(W)
{
    auto meshData = primitives::Cube(1.f);
    m_mesh = W.Engine().MeshSystem().CreateMesh({.Data = &meshData, .DebugName = "CubeMesh"});
}
