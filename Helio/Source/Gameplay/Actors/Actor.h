#pragma once

#include <Gameplay/World/HelioObject.h>
#include <Math/Transform.h>

namespace helio::gameplay
{
    class HelioWorld;
    
    class Actor : public HelioObject
    {
    public:

        explicit Actor(HelioWorld& W) : m_world(&W) {}
        
        [[nodiscard]] Transform& GetTransform() { return m_transform; }
        [[nodiscard]] const Transform& GetTransform() const { return m_transform; }
        void SetTransform(const Transform& T) { m_transform = T; }

        [[nodiscard]] HelioWorld& GetWorld() const { return *m_world; }
        
        virtual void BeginPlay() {}
        virtual void Tick(float DeltaTime) {}
        virtual void EndPlay() {}

        void EnableTick() {}
        void DisableTick() {}
    
        bool IsTickEnabled() const noexcept { return false; } 
    
    protected:
        HelioWorld* m_world = nullptr;
        Transform m_transform = Transform();
    };
}