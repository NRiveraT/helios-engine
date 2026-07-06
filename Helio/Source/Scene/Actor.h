/// @file Actor.h
/// @brief Scene-graph node: transform hierarchy + lifecycle.
///
/// Every Actor carries a LOCAL transform (relative to its parent, or to the
/// world when unparented) and a lazily-cached WORLD transform. Mutating any
/// local component marks this actor and all descendants dirty; the world
/// transform recomposes on next read (`parentWorld * local`), so deep chains
/// cost nothing until someone actually asks.
///
/// Ownership vs. topology: `HelioWorld` owns every actor's lifetime (flat
/// `unique_ptr` storage). Parent/child links are pure topology on top —
/// attaching never transfers ownership. Destroying an actor through
/// `HelioWorld::DestroyActor` destroys its whole subtree; the Actor
/// destructor itself never touches hierarchy links (the World severs them),
/// so teardown order can never chase a dangling parent.
///
/// API convention mirrors UE:
/// - `AddWorldOffset` moves along WORLD axes; `AddLocalOffset` along the
///   actor's OWN axes.
/// - `AddWorldRotation` pre-multiplies (world frame); `AddLocalRotation`
///   post-multiplies (local frame).
/// - Setting a WORLD property on a parented actor converts through the
///   parent's world transform (exact for position/rotation; non-uniform
///   parent scale carries the `Transform` composition caveat).
#pragma once

#include <Core/Math/Transform.h>
#include <Scene/HelioObject.h>

#include <cstdint>
#include <string>
#include <vector>

namespace helio::scene
{
    class HelioWorld;
    class SceneRenderer;

    class Actor : public HelioObject
    {
    public:
        enum class AttachRule : uint8_t
        {
            /// Recompute the local transform so the actor does not move.
            KeepWorld,
            /// Keep the local transform verbatim; the actor jumps to the new frame.
            KeepLocal,
        };

        explicit Actor(HelioWorld& W);
        ~Actor() override = default;

        // ---- Identity -----------------------------------------------------

        [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
        void SetName(std::string Name) { m_Name = std::move(Name); }

        [[nodiscard]] HelioWorld& GetWorld() const noexcept { return *m_World; }

        // ---- Hierarchy ----------------------------------------------------

        [[nodiscard]] Actor* GetParent() const noexcept { return m_Parent; }
        [[nodiscard]] const std::vector<Actor*>& GetChildren() const noexcept { return m_Children; }
        [[nodiscard]] bool IsDescendantOf(const Actor& Ancestor) const noexcept;

        /// Parent this actor under `Parent`. Returns false (and does nothing)
        /// if the attachment would create a cycle or self-parent.
        bool AttachTo(Actor& Parent, AttachRule Rule = AttachRule::KeepWorld);

        /// Unparent (become a root actor).
        void Detach(AttachRule Rule = AttachRule::KeepWorld);

        // ---- Local transform (parent space) --------------------------------

        [[nodiscard]] const Transform& GetLocalTransform() const noexcept { return m_Local; }
        void SetLocalTransform(const Transform& T);
        [[nodiscard]] float3 GetLocalPosition() const noexcept { return m_Local.Position; }
        void SetLocalPosition(float3 P);
        [[nodiscard]] float4 GetLocalRotation() const noexcept { return m_Local.Rotation; }
        void SetLocalRotation(float4 Q);
        [[nodiscard]] float3 GetLocalScale() const noexcept { return m_Local.Scale; }
        void SetLocalScale(float3 S);

        // ---- World transform (cached, lazily composed) ----------------------

        [[nodiscard]] const Transform& GetWorldTransform() const;
        void SetWorldTransform(const Transform& T);
        [[nodiscard]] float3 GetWorldPosition() const { return GetWorldTransform().Position; }
        void SetWorldPosition(float3 P);
        [[nodiscard]] float4 GetWorldRotation() const { return GetWorldTransform().Rotation; }
        void SetWorldRotation(float4 Q);

        // ---- Deltas ---------------------------------------------------------

        /// Move along world axes.
        void AddWorldOffset(float3 Delta);
        /// Rotate about world axes (pre-multiply).
        void AddWorldRotation(float4 Q);
        inline void AddWorldRotation(float3 Axis, float Radians)
        {
            AddWorldRotation(QuatFromAxisAngle(Axis, Radians));
        }
        /// Move along the actor's own axes (rotation applied, scale not — UE semantics).
        void AddLocalOffset(float3 Delta);
        /// Rotate about the actor's own axes (post-multiply).
        void AddLocalRotation(float4 Q);
        inline void AddLocalRotation(float3 Axis, float Radians)
        {
            AddLocalRotation(QuatFromAxisAngle(Axis, Radians));
        }

        // ---- World-space basis ----------------------------------------------

        [[nodiscard]] float3 GetForward() const { return GetWorldTransform().GetForward(); }
        [[nodiscard]] float3 GetRight() const { return GetWorldTransform().GetRight(); }
        [[nodiscard]] float3 GetUp() const { return GetWorldTransform().GetUp(); }

        // ---- Lifecycle ------------------------------------------------------

        virtual void BeginPlay() {}
        virtual void Tick(float DeltaTime) { (void)DeltaTime; }
        virtual void EndPlay() {}

        /// Called once per frame by the SceneRenderer to collect draw
        /// submissions. Override in renderable actors and call
        /// `Renderer.SubmitMesh(...)`.
        virtual void OnRender(SceneRenderer& Renderer) { (void)Renderer; }

        void SetTickEnabled(bool Enabled) noexcept { m_TickEnabled = Enabled; }
        [[nodiscard]] bool IsTickEnabled() const noexcept { return m_TickEnabled; }

        /// True once `HelioWorld::DestroyActor` has claimed this actor; it
        /// stops ticking/rendering and is deleted at the end of the current
        /// (or next) `HelioWorld::Tick`.
        [[nodiscard]] bool IsPendingDestroy() const noexcept { return m_PendingDestroy; }

    private:
        friend class HelioWorld;

        /// Invalidate this actor's cached world transform and every
        /// descendant's. Early-outs on already-dirty subtrees.
        void MarkWorldDirty() const noexcept;

        HelioWorld* m_World = nullptr;
        Actor* m_Parent = nullptr;
        std::vector<Actor*> m_Children;
        std::string m_Name;

        Transform m_Local{};
        mutable Transform m_WorldCache{};
        mutable bool m_WorldDirty = true;

        bool m_TickEnabled = true;
        bool m_PendingDestroy = false;
    };
} // namespace helio::scene
