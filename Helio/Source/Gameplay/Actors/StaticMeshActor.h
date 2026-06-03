#pragma once
#include "Actor.h"
#include <resource/Mesh.h>

#include "Interfaces/IRenderable.h"

using namespace helio::resource;

namespace helio::gameplay
{
    class StaticMeshActor : public Actor, public IRenderable
    {
    public:
        explicit StaticMeshActor(HelioWorld& W);
        
        [[nodiscard]] Mesh& GetMesh() { return m_mesh; }
        void SetMesh(Mesh M) { m_mesh = M; }
        
        void OnRender() override;
        void SubmitToRenderQueue() override;

    protected:
        Mesh m_mesh;
    };
}
