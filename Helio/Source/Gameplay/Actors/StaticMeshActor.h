#pragma once
#include "Actor.h"
#include <resource/Mesh.h>
#include <resource/Material.h>

#include "Interfaces/IRenderable.h"

using namespace helio::resource;

namespace helio::gameplay
{
    class StaticMeshActor : public Actor
    {
    public:
        explicit StaticMeshActor(HelioWorld& W, Mesh Mesh);

        [[nodiscard]] Mesh&     GetMesh()                       { return m_Mesh; }
        [[nodiscard]] Material& GetMaterial()                   { return m_Material; }
        [[nodiscard]] const Material& GetMaterial() const       { return m_Material; }
        void SetMesh(const Mesh& M)                             { m_Mesh = M; }
        void SetMaterial(const Material& Mat)                   { m_Material = Mat; }

        void OnRender() override;

    protected:
        Mesh     m_Mesh;
        Material m_Material;
    };
}
