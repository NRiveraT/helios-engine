/// @file StaticMeshActor.h
/// @brief Actor that renders one or more mesh sections (mesh + material).
///
/// A static mesh actor is a list of `resource::MeshSection`s — each a mesh
/// paired with the material it draws with. A single primitive spawns one
/// section; an imported glTF model spawns one per primitive (each carrying its
/// own material, resolved at import time via `MeshSystem::LoadModel`). The
/// section list is the actor's own editable copy, so per-actor material
/// overrides are just edits to a section.
#pragma once

#include <Scene/Actor.h>

#include <Resource/Material.h>
#include <Resource/Mesh.h>

#include <utility>
#include <vector>

namespace helio::scene
{
    class StaticMeshActor : public Actor
    {
    public:
        /// Empty actor — add sections with `AddMeshSection` / `SetSections`.
        explicit StaticMeshActor(HelioWorld& W);
        /// Convenience: start with one section (material defaults if omitted).
        StaticMeshActor(HelioWorld& W, std::string SectionName, const resource::Mesh& Mesh, const resource::Material& Material = {});

        // ---- Sections ------------------------------------------------------

        void AddMeshSection(const resource::MeshSection& Section)
        {
            m_Sections.push_back(Section);
        }
        void AddMeshSection(std::string Name, const resource::Mesh& Mesh, const resource::Material& Material = {})
        {
            m_Sections.push_back({Name, Mesh, Material});
        }
        void SetSections(std::vector<resource::MeshSection> Sections) noexcept
        {
            m_Sections = std::move(Sections);
        }
        void ClearSections() noexcept { m_Sections.clear(); }

        [[nodiscard]] std::vector<resource::MeshSection>& GetMeshSections() noexcept { return m_Sections; }
        [[nodiscard]] const std::vector<resource::MeshSection>& GetMeshSections() const noexcept { return m_Sections; }
        [[nodiscard]] size_t NumSections() const noexcept { return m_Sections.size(); }

        void OnRender(SceneRenderer& Renderer) override;

    protected:
        std::vector<resource::MeshSection> m_Sections;
    };
} // namespace helio::scene
