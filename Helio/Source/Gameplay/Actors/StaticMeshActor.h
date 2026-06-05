#pragma once
#include "Actor.h"
#include <resource/Mesh.h>

#include "Interfaces/IRenderable.h"

using namespace helio::resource;

namespace helio::gameplay
{
    class StaticMeshActor : public Actor
    {
    public:
        explicit StaticMeshActor(HelioWorld& W, Mesh Mesh);

        [[nodiscard]] Mesh& GetMesh() { return m_Mesh; }
        Material& GetMaterial() { return m_Mesh.m_Material; }
        void SetMesh(const Mesh& M) { m_Mesh = M; }

        void OnRender() override;

    protected:
        Mesh m_Mesh;
    };
}
