#pragma once

#include <Gameplay/World/HelioObject.h>
#include <Math/Transform.h>

namespace helio::gameplay
{
    class HelioWorld;
    
    class Actor : public HelioObject
    {
    public:

        explicit Actor(HelioWorld& W) : m_World(&W) {}
        
        [[nodiscard]] Transform& GetTransform() { return m_Transform; }
        [[nodiscard]] const Transform& GetTransform() const { return m_Transform; }
        void SetTransform(const Transform& T) { m_Transform = T; }

        [[nodiscard]] HelioWorld& GetWorld() const { return *m_World; }
        
        virtual void BeginPlay() {}
        virtual void Tick(float DeltaTime) {}
        virtual void EndPlay() {}

        virtual void OnRender() {}
        
        void EnableTick() {}
        void DisableTick() {}
    
        bool IsTickEnabled() const noexcept { return false; }

        float3 GetActorForwardVector() const noexcept { return m_Transform.GetForward(); }
        float3 GetActorRightVector()   const noexcept { return m_Transform.GetRight(); }
        float3 GetActorUpVector()      const noexcept { return m_Transform.GetUp(); }

    protected:
        HelioWorld* m_World = nullptr;
        Transform m_Transform = Transform();
    };
}