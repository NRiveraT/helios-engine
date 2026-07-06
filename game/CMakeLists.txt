add_executable(Game
    Source/main.cpp
)

target_link_libraries(Game PRIVATE Helio)

# Copy the project descriptor next to the binary at build time.
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/Game.helioproject
        $<TARGET_FILE_DIR:Game>/Game.helioproject
    VERBATIM
)

# Copy required runtime DLLs (Tracy, SDL3, spdlog, etc.) next to Game.exe.
# vcpkg's x64-windows triplet produces shared libs; this is the canonical
# pattern for getting them deployed next to the binary in CMake 3.21+.
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:Game>
        $<TARGET_FILE_DIR:Game>
    COMMAND_EXPAND_LISTS
)

# Copy game assets (test glTF models, etc.) next to the binary so the demo can
# load them by relative path (e.g. "Assets/Crate.gltf").
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/Assets
        $<TARGET_FILE_DIR:Game>/Assets
    VERBATIM
)

# Compiled engine shaders deploy under Shaders/<rel>.spv next to Game.exe.
#
# Game depends on Helio.Shaders, so editing any .slang propagates:
#   .slang change -> slangc re-runs (CompileShaders custom_command DEPENDS .slang)
#                 -> Helio.Shaders considered out of date
#                 -> Game considered out of date (via add_dependencies)
#                 -> POST_BUILD copy fires, deploys fresh .spv into bin/Debug/Shaders/
# Verified empirically (timestamps match across `Helio/Shaders/` and `bin/Debug/Shaders/`).
add_dependencies(Game Helio.Shaders)
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/Helio/Shaders
        $<TARGET_FILE_DIR:Game>/Shaders
    VERBATIM
)

set_target_properties(Game PROPERTIES
    FOLDER "Game"
    VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:Game>
)
