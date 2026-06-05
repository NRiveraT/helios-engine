#include "StaticMeshActor.h"

#include "World/HelioWorld.h"

namespace helio::gameplay
{
    StaticMeshActor::StaticMeshActor(HelioWorld& W, Mesh Mesh) : Actor(W)
    {
        SetMesh(Mesh);
    }

    void StaticMeshActor::OnRender()
    {
        m_World->Engine().Renderer().SubmitMesh(m_Mesh, GetTransform());
    }
}
