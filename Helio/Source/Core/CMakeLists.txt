find_package(spdlog CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)

# Workaround: vcpkg's hlslpp-config.cmake ships with a typo ("INTERACE" instead
# of "INTERFACE") AND uses a triple-colon IMPORTED target name that CMake
# rejects. hlslpp is header-only — wire it up manually instead.
find_path(HLSLPP_INCLUDE_BASE
    NAMES hlslpp/hlsl++.h
    REQUIRED
    DOC "Directory containing hlslpp/hlsl++.h (vcpkg-installed)"
)
add_library(hlslpp INTERFACE)
target_include_directories(hlslpp INTERFACE ${HLSLPP_INCLUDE_BASE})

helio_add_module(Core
    SOURCES
        Logging/Log.cpp
        Handles/Freelist.cpp
        Time/Clock.cpp
        Math/Math.cpp
        Math/Transform.cpp
        Types/EngineConfig.h
    PUBLIC_LINK
        spdlog::spdlog
        hlslpp
        meshoptimizer::meshoptimizer
)

# Tracy is pulled in by Core/Profile (compiled out via HELIO_TRACY=OFF).
if(HELIO_TRACY)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(Helio.Core PUBLIC Tracy::TracyClient)
    target_compile_definitions(Helio.Core PUBLIC HELIO_TRACY_ENABLED=1)
else()
    target_compile_definitions(Helio.Core PUBLIC HELIO_TRACY_ENABLED=0)
endif()
