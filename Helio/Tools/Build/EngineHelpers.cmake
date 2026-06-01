# EngineHelpers.cmake
# Helper functions for defining Helio engine modules.
#
# Usage:
#   helio_add_module(Core
#       SOURCES
#           Logging/Log.cpp
#           Math/Math.cpp
#       PUBLIC_LINK
#           spdlog::spdlog
#   )
#
# This produces a STATIC library target named "Helio.Core" with the source
# directory added to its public include path. Stub.cpp is automatically
# added if SOURCES is empty (so a placeholder module still produces a
# linkable archive).

function(helio_add_module MODULE_NAME)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs SOURCES PUBLIC_LINK PRIVATE_LINK PUBLIC_INCLUDE PRIVATE_INCLUDE PUBLIC_DEFINE PRIVATE_DEFINE)
    cmake_parse_arguments(HELIO_MOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(TARGET_NAME "Helio.${MODULE_NAME}")

    if(NOT HELIO_MOD_SOURCES)
        set(HELIO_MOD_SOURCES Stub.cpp)
    endif()

    add_library(${TARGET_NAME} STATIC ${HELIO_MOD_SOURCES})

    # PUBLIC include base is the Source/ root, so callers (and the module itself)
    # use <Core/...>, <RHI/...>, <Renderer/...> uniformly. The module's own
    # directory is added too so internal sources can use short paths like
    # <Logging/Log.h> from inside Core/.
    get_filename_component(_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
    while(NOT EXISTS "${_SOURCE_ROOT}/Core" AND NOT EXISTS "${_SOURCE_ROOT}/RHI")
        get_filename_component(_PARENT ${_SOURCE_ROOT}/.. ABSOLUTE)
        if(_PARENT STREQUAL _SOURCE_ROOT)
            message(FATAL_ERROR "helio_add_module: could not locate Helio/Source/ from ${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        set(_SOURCE_ROOT ${_PARENT})
    endwhile()

    target_include_directories(${TARGET_NAME}
        PUBLIC
            ${_SOURCE_ROOT}
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${HELIO_MOD_PUBLIC_INCLUDE}
        PRIVATE
            ${HELIO_MOD_PRIVATE_INCLUDE}
    )

    if(HELIO_MOD_PUBLIC_LINK)
        target_link_libraries(${TARGET_NAME} PUBLIC ${HELIO_MOD_PUBLIC_LINK})
    endif()
    if(HELIO_MOD_PRIVATE_LINK)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HELIO_MOD_PRIVATE_LINK})
    endif()
    if(HELIO_MOD_PUBLIC_DEFINE)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${HELIO_MOD_PUBLIC_DEFINE})
    endif()
    if(HELIO_MOD_PRIVATE_DEFINE)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${HELIO_MOD_PRIVATE_DEFINE})
    endif()

    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Helio")
endfunction()
