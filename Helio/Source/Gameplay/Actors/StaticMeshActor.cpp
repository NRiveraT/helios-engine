#include "StaticMeshActor.h"

#include "World/HelioWorld.h"

namespace helio::gameplay
{
    StaticMeshActor::StaticMeshActor(HelioWorld& W) : Actor(W)
    {}

    void StaticMeshActor::OnRender()
    {
        m_world->Engine().Renderer().SubmitMesh(m_mesh, GetTransform());
    }

    void StaticMeshActor::SubmitToRenderQueue()
    {
        m_world->Engine().Renderer().SubmitMesh(m_mesh, GetTransform());
    }
}
