#include "Actor.h"

#include <Core/Logging/Log.h>

#include <algorithm>

namespace helio::scene
{
    Actor::Actor(HelioWorld& W) : m_World(&W)
    {
    }

    // ---- Hierarchy ----------------------------------------------------------

    bool Actor::IsDescendantOf(const Actor& Ancestor) const noexcept
    {
        for (const Actor* Node = m_Parent; Node != nullptr; Node = Node->m_Parent)
        {
            if (Node == &Ancestor)
            {
                return true;
            }
        }
        return false;
    }

    bool Actor::AttachTo(Actor& Parent, AttachRule Rule)
    {
        if (&Parent == this || Parent.IsDescendantOf(*this))
        {
            HELIO_LOG_WARN("Scene", "AttachTo rejected: '{}' -> '{}' would create a cycle",
                           m_Name, Parent.m_Name);
            return false;
        }
        // Never reparent into or out of a subtree that is already scheduled
        // for destruction: attaching a dying node would either leak it (the
        // flush walk can't reach it) or, in reverse, drag a live actor into
        // the doomed subtree and free it out from under its owner.
        if (m_PendingDestroy || Parent.m_PendingDestroy)
        {
            HELIO_LOG_WARN("Scene", "AttachTo rejected: '{}' -> '{}' involves a pending-destroy actor",
                           m_Name, Parent.m_Name);
            return false;
        }
        if (&Parent == m_Parent)
        {
            return true;
        }

        // Capture world placement before the reparent so KeepWorld can restore it.
        const Transform World = GetWorldTransform();

        if (m_Parent != nullptr)
        {
            auto& Siblings = m_Parent->m_Children;
            Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), this), Siblings.end());
        }
        m_Parent = &Parent;
        Parent.m_Children.push_back(this);

        if (Rule == AttachRule::KeepWorld)
        {
            SetWorldTransform(World);
        }
        else
        {
            MarkWorldDirty();
        }
        return true;
    }

    void Actor::Detach(AttachRule Rule)
    {
        if (m_Parent == nullptr)
        {
            return;
        }
        // A pending-destroy node stays wired to its parent until the flush
        // walks and frees the whole subtree together; unhooking it here would
        // strand it (never EndPlay'd, never freed until Shutdown).
        if (m_PendingDestroy)
        {
            return;
        }

        const Transform World = GetWorldTransform();

        auto& Siblings = m_Parent->m_Children;
        Siblings.erase(std::remove(Siblings.begin(), Siblings.end(), this), Siblings.end());
        m_Parent = nullptr;

        if (Rule == AttachRule::KeepWorld)
        {
            m_Local = World; // no parent: local IS world
        }
        MarkWorldDirty();
    }

    // ---- Transforms ----------------------------------------------------------

    void Actor::MarkWorldDirty() const noexcept
    {
        if (m_WorldDirty)
        {
            return; // subtree is already dirty — children were marked when we were
        }
        m_WorldDirty = true;
        for (const Actor* Child : m_Children)
        {
            Child->MarkWorldDirty();
        }
    }

    const Transform& Actor::GetWorldTransform() const
    {
        if (m_WorldDirty)
        {
            m_WorldCache = (m_Parent != nullptr)
                               ? m_Parent->GetWorldTransform() * m_Local
                               : m_Local;
            m_WorldDirty = false;
        }
        return m_WorldCache;
    }

    void Actor::SetLocalTransform(const Transform& T)
    {
        m_Local = T;
        MarkWorldDirty();
    }

    void Actor::SetLocalPosition(float3 P)
    {
        m_Local.Position = P;
        MarkWorldDirty();
    }

    void Actor::SetLocalRotation(float4 Q)
    {
        m_Local.Rotation = QuatNormalize(Q);
        MarkWorldDirty();
    }

    void Actor::SetLocalScale(float3 S)
    {
        m_Local.Scale = S;
        MarkWorldDirty();
    }

    void Actor::SetWorldTransform(const Transform& T)
    {
        m_Local = (m_Parent != nullptr)
                      ? m_Parent->GetWorldTransform().Inverse() * T
                      : T;
        MarkWorldDirty();
    }

    void Actor::SetWorldPosition(float3 P)
    {
        // InverseTransformPoint is exact even under non-uniform parent scale.
        m_Local.Position = (m_Parent != nullptr)
                               ? m_Parent->GetWorldTransform().InverseTransformPoint(P)
                               : P;
        MarkWorldDirty();
    }

    void Actor::SetWorldRotation(float4 Q)
    {
        m_Local.Rotation = (m_Parent != nullptr)
                               ? QuatNormalize(QuatMul(QuatConjugate(m_Parent->GetWorldTransform().Rotation), Q))
                               : QuatNormalize(Q);
        MarkWorldDirty();
    }

    void Actor::AddWorldOffset(float3 Delta)
    {
        SetWorldPosition(GetWorldPosition() + Delta);
    }

    void Actor::AddWorldRotation(float4 Q)
    {
        SetWorldRotation(QuatMul(Q, GetWorldRotation()));
    }

    void Actor::AddLocalOffset(float3 Delta)
    {
        // Delta expressed in the actor's own frame (rotation only — UE semantics).
        m_Local.Position += m_Local.RotateVector(Delta);
        MarkWorldDirty();
    }

    void Actor::AddLocalRotation(float4 Q)
    {
        m_Local.RotateLocal(Q);
        MarkWorldDirty();
    }
} // namespace helio::scene
