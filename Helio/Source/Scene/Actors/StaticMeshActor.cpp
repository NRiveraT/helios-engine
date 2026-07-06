#include "StaticMeshActor.h"

#include <Scene/SceneRenderer.h>

namespace helio::scene
{
    StaticMeshActor::StaticMeshActor(HelioWorld& W)
        : Actor(W)
    {
    }

    StaticMeshActor::StaticMeshActor(HelioWorld& W, std::string SectionName, const resource::Mesh& Mesh, const resource::Material& Material) : Actor(W)
    {
        m_Sections.push_back({SectionName, Mesh, Material});
    }

    void StaticMeshActor::OnRender(SceneRenderer& Renderer)
    {
        const Transform& World = GetWorldTransform();
        for (const resource::MeshSection& Section : m_Sections)
        {
            Renderer.SubmitMesh(Section.Mesh, Section.Material, World);
        }
    }
} // namespace helio::scene
