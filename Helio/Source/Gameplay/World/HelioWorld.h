/// @file HelioWorld.h
/// @brief Runtime container for Actors.
///
/// One World instance per running play session. Owns Actor lifetimes,
/// drives the per-phase tick, exposes spawn / query / destroy.
///
/// STUB — public API will fill in as the gameplay system grows. See
/// `Helio/Docs/Architecture.md` for the planned shape.
#pragma once

#include <memory>
#include <vector>
#include "Actors/Actor.h"
#include <Gameplay/HelioEngine.h>

namespace helio::gameplay
{
    class HelioEngine;

    class HelioWorld
    {
    public:
        explicit HelioWorld(HelioEngine& engine)
            : m_engine(&engine)
        {}

        ~HelioWorld();

        HelioWorld(const HelioWorld&) = delete;
        HelioWorld& operator=(const HelioWorld&) = delete;

        HelioEngine& Engine() const noexcept { return *m_engine; }
        std::vector<std::unique_ptr<Actor>>& GetWorldActors() noexcept { return m_actors; }
        resource::MeshSystem& MeshSystem() const noexcept { return m_engine->MeshSystem(); }

        /// Spawn an Actor of type `T`. The World takes ownership; the returned
        /// pointer is valid until the actor is destroyed (or the World shuts down).
        template <typename T, typename... Args>
        T* SpawnActor(Args&&... args)
        {
            auto Owned = std::make_unique<T>(std::forward<Args>(args)...);
            T* Raw = Owned.get();
            Raw->BeginPlay();
            m_actors.push_back(std::move(Owned));
            return Raw;
        }

        template <typename T>
        [[nodiscard]] T* GetActorsByClass()
        {
            for (auto& actor : m_actors)
            {
                if (T* r = dynamic_cast<T*>(actor.get()))
                {
                    return r;
                }
            }
            
            return nullptr;
        }
        
        void Startup();

        /// Per-phase tick — game loop calls this from main(). Phase enum lives
        /// in `TickPhase.h` (added when the second phase becomes necessary).
        void Tick(float DeltaSeconds);
        
        /// Destroy every actor (in reverse spawn order) and clear the world.
        void Shutdown();

    private:
        HelioEngine* m_engine;

        std::vector<std::unique_ptr<Actor>> m_actors;
    };
} // namespace helio
