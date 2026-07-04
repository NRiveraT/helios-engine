#pragma once

#include <string>

namespace helio::core
{
    struct EngineConfig
    {
        std::string Title{"Helio Engine"};
        int Width{1920};
        int Height{1080};

        bool ValidationLayers{true};
        bool Raytracing{true};
    };
}
