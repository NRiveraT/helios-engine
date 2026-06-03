#include "HelioWorld.h"
#include <RenderGraph.h>

namespace helio::gameplay
{
    HelioWorld::~HelioWorld() { Shutdown(); }

    void HelioWorld::Startup()
    {
    }

    void HelioWorld::Tick(float DeltaSeconds)
    {
        // Iterate by index to remain stable against actors spawning more actors
        // during their Tick (the vector may grow; new entries are unticked this frame).
        const size_t Count = m_actors.size();
        for (size_t I = 0; I < Count; ++I)
        {
            if (Actor* A = m_actors[I].get())
            {
                A->Tick(DeltaSeconds);
            }
        }
    }
    
    void HelioWorld::Shutdown()
    {
        // Reverse-spawn order so dependents tear down before what they referenced.
        for (auto It = m_actors.rbegin(); It != m_actors.rend(); ++It)
        {
            if (Actor* A = It->get())
            {
                A->EndPlay();
            }
        }
        m_actors.clear();
    }
} // namespace helio
