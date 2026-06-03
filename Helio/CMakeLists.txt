# Helio engine — aggregate target.
# Each Source/* module is its own STATIC library. The "Helio" INTERFACE target
# below aggregates them so consumers (the game project) just do:
#   target_link_libraries(Game PRIVATE Helio)

add_library(Helio INTERFACE)

add_subdirectory(Source/Core)
add_subdirectory(Source/Platform/Windows)
add_subdirectory(Source/RHI)
add_subdirectory(Source/Renderer)
add_subdirectory(Source/Input)
add_subdirectory(Source/Resource)
add_subdirectory(Source/Gameplay)

target_link_libraries(Helio INTERFACE
    Helio.Core
    Helio.Platform.Windows
    Helio.RHI
    Helio.Renderer
    Helio.Input
    Helio.Resource
    Helio.Gameplay
)

target_include_directories(Helio INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

set_target_properties(Helio PROPERTIES FOLDER "Helio")

# Engine-side shaders (verification triangle + fullscreen blit).
helio_compile_shaders(TARGET Helio.Shaders
    ROOT         ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
    EXCLUDE_DIRS Common                              # imports-only, never compiled standalone
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Common
    # ROOT-discovered paths are relative to CMAKE_CURRENT_SOURCE_DIR and already
    # start with `Shaders/...`, so the compiled output tree re-roots at `Shaders/`
    # under OUTPUT_DIR. Game's POST_BUILD then copies bin/Debug/Shaders/...
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/Helio
)
