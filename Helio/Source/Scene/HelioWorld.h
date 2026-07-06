/// @file HelioWorld.h
/// @brief Runtime container for Actors.
///
/// One World instance per running play session. Owns every Actor's lifetime
/// in a flat `unique_ptr` vector; the scene-graph hierarchy (`Actor::AttachTo`)
/// is pure topology layered on top and never affects ownership.
///
/// Destruction is deferred: `DestroyActor` marks the actor's whole subtree
/// pending-destroy (it immediately stops ticking/rendering) and the actual
/// `EndPlay` + deletion happens at the end of the current `Tick`. That makes
/// it safe to destroy actors from inside any actor's `Tick` or from editor UI.
#pragma once

#include <Scene/Actor.h>

#include <memory>
#include <utility>
#include <vector>

namespace helio::scene
{
    class HelioWorld
    {
    public:
        HelioWorld() = default;
        ~HelioWorld();

        HelioWorld(const HelioWorld&) = delete;
        HelioWorld& operator=(const HelioWorld&) = delete;

        /// Spawn an Actor of type `T`. The World takes ownership; the returned
        /// pointer is valid until the actor is destroyed (or the World shuts down).
        ///
        /// `*this` is automatically passed as `T`'s first constructor argument
        /// (the world reference Actors require). Any additional ctor params
        /// are forwarded after it.
        ///
        ///     World.SpawnActor<StaticMeshActor>(MeshHandle);
        ///       → new StaticMeshActor(World, MeshHandle);
        template <typename T, typename... Args>
        T* SpawnActor(Args&&... args)
        {
            auto Owned = std::make_unique<T>(*this, std::forward<Args>(args)...);
            T* Raw = Owned.get();
            m_Actors.push_back(std::move(Owned));
            Raw->BeginPlay();
            return Raw;
        }

        /// Spawn + name in one call (names show up in the editor scene tree).
        template <typename T, typename... Args>
        T* SpawnActorNamed(std::string ActorName, Args&&... args)
        {
            T* Raw = SpawnActor<T>(std::forward<Args>(args)...);
            Raw->SetName(std::move(ActorName));
            return Raw;
        }

        template <typename T>
        [[nodiscard]] T* GetActorByClass() const
        {
            for (const auto& A : m_Actors)
            {
                if (T* Match = dynamic_cast<T*>(A.get()))
                {
                    if (!Match->IsPendingDestroy())
                    {
                        return Match;
                    }
                }
            }
            return nullptr;
        }

        /// Resolve a live actor by its stable `HelioObject::Id`. Returns
        /// nullptr if no actor has that id or the actor is pending destruction.
        /// This is the lifetime-safe way to keep a reference to a specific
        /// actor across frames — hold the `uint64_t` id, not a raw pointer,
        /// so an editor deletion (or any `DestroyActor`) can't dangle it.
        [[nodiscard]] Actor* FindActorById(uint64_t Id) const
        {
            if (Id == 0)
            {
                return nullptr;
            }
            for (const auto& A : m_Actors)
            {
                if (A && A->Id() == Id && !A->IsPendingDestroy())
                {
                    return A.get();
                }
            }
            return nullptr;
        }

        /// Typed convenience: resolve by id AND dynamic-cast to `T`.
        template <typename T>
        [[nodiscard]] T* FindActorByIdAs(uint64_t Id) const
        {
            return dynamic_cast<T*>(FindActorById(Id));
        }

        [[nodiscard]] const std::vector<std::unique_ptr<Actor>>& GetActors() const noexcept
        {
            return m_Actors;
        }

        /// Queue `A` and its entire subtree for destruction. The subtree stops
        /// ticking/rendering immediately; `EndPlay` runs (children before
        /// parents) and memory is released at the end of the current Tick.
        void DestroyActor(Actor& A);

        /// Tick every live tick-enabled actor, then flush the destroy queue.
        void Tick(float DeltaSeconds);

        /// Destroy every actor (EndPlay in reverse spawn order) and clear the world.
        void Shutdown();

    private:
        void FlushDestroyQueue();

        std::vector<std::unique_ptr<Actor>> m_Actors;
        std::vector<Actor*> m_DestroyQueue; // subtree roots, deduplicated by the pending flag
    };
} // namespace helio::scene
