#pragma once

namespace helio::gameplay
{
    class IRenderable
    {
    public:
        virtual ~IRenderable() = default;
        
        virtual void OnRender() = 0;
        virtual void SubmitToRenderQueue() = 0;
    };
};
