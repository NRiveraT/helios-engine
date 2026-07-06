#include "HelioWorld.h"

#include <algorithm>
#include <unordered_set>

namespace helio::scene
{
    HelioWorld::~HelioWorld() { Shutdown(); }

    void HelioWorld::DestroyActor(Actor& A)
    {
        if (A.m_PendingDestroy)
        {
            return;
        }
        // Claim the whole subtree so none of it ticks/renders from here on.
        // Only the root goes in the queue — FlushDestroyQueue walks children.
        A.m_PendingDestroy = true;
        for (Actor* Child : A.m_Children)
        {
            DestroyActor(*Child);
        }
        // If the parent is already pending, this node will be reached through
        // it; queuing it again would double-EndPlay.
        if (A.m_Parent == nullptr || !A.m_Parent->m_PendingDestroy)
        {
            m_DestroyQueue.push_back(&A);
        }
    }

    void HelioWorld::Tick(float DeltaSeconds)
    {
        // Iterate by index to remain stable against actors spawning more actors
        // during their Tick (the vector may grow; new entries are unticked this frame).
        const size_t Count = m_Actors.size();
        for (size_t I = 0; I < Count; ++I)
        {
            Actor* A = m_Actors[I].get();
            if (A != nullptr && A->IsTickEnabled() && !A->IsPendingDestroy())
            {
                A->Tick(DeltaSeconds);
            }
        }

        FlushDestroyQueue();
    }

    void HelioWorld::FlushDestroyQueue()
    {
        if (m_DestroyQueue.empty())
        {
            return;
        }

        // Depth-first, children before parents: EndPlay the subtree post-order,
        // then sever links and release. Actors spawned during EndPlay are left
        // alive (they join the world normally).
        std::vector<Actor*> PostOrder;
        const auto Collect = [&PostOrder](auto&& Self, Actor& Node) -> void {
            for (Actor* Child : Node.m_Children)
            {
                Self(Self, *Child);
            }
            PostOrder.push_back(&Node);
        };

        // Take the queue by move — EndPlay may legally queue more destroys.
        std::vector<Actor*> Roots = std::move(m_DestroyQueue);
        m_DestroyQueue.clear();

        for (Actor* Root : Roots)
        {
            // Detach the subtree root from any surviving parent.
            if (Root->m_Parent != nullptr)
            {
                auto& Siblings = Root->m_Parent->m_Children;
                Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), Root), Siblings.end());
                Root->m_Parent = nullptr;
            }
            Collect(Collect, *Root);
        }

        for (Actor* Dead : PostOrder)
        {
            Dead->EndPlay();
        }
        for (Actor* Dead : PostOrder)
        {
            // Sever remaining links so no destructor-order issue can bite.
            Dead->m_Parent = nullptr;
            Dead->m_Children.clear();
            for (auto It = m_Actors.begin(); It != m_Actors.end(); ++It)
            {
                if (It->get() == Dead)
                {
                    m_Actors.erase(It);
                    break;
                }
            }
        }

        // Anything queued during EndPlay gets flushed next Tick.
    }

    void HelioWorld::Shutdown()
    {
        m_DestroyQueue.clear();

        // EndPlay every actor exactly once, tolerating actors that spawn more
        // actors from inside their own EndPlay (a legal pattern — the same one
        // FlushDestroyQueue supports). We process in waves: each wave EndPlays
        // the actors not yet ended (reverse order within the wave so dependents
        // tear down first), and any actor spawned during a wave is picked up by
        // the next. Iterating a snapshot per wave keeps us safe against the
        // m_Actors reallocation that push_back during EndPlay would cause.
        std::unordered_set<Actor*> Ended;
        while (true)
        {
            std::vector<Actor*> Wave;
            Wave.reserve(m_Actors.size());
            for (auto& A : m_Actors)
            {
                if (A && Ended.find(A.get()) == Ended.end())
                {
                    Wave.push_back(A.get());
                }
            }
            if (Wave.empty())
            {
                break;
            }
            for (auto It = Wave.rbegin(); It != Wave.rend(); ++It)
            {
                Ended.insert(*It);
                (*It)->EndPlay();
            }
        }

        // Sever all hierarchy links before releasing memory — unique_ptr
        // destruction order must not matter.
        for (auto& A : m_Actors)
        {
            if (A)
            {
                A->m_Parent = nullptr;
                A->m_Children.clear();
            }
        }
        m_Actors.clear();
        m_DestroyQueue.clear();
    }
} // namespace helio::scene
