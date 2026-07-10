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
        // Compose the actor's world transform with each section's baked local
        // placement (identity for hand-built actors, the glTF node transform for
        // imported ones), as matrices so non-uniform scale stays exact.
        const float4x4 ActorWorld = GetWorldTransform().ToMatrix();
        for (const resource::MeshSection& Section : m_Sections)
        {
            Renderer.SubmitMesh(Section.Mesh, Section.Material, hlslpp::mul(ActorWorld, Section.LocalTransform));
        }
    }
} // namespace helio::scene
