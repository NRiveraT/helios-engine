# CompileShaders.cmake
# Compiles .slang files to .spv at build time using slangc from the Vulkan SDK.
# Each .slang produces ONE .spv that embeds every `[shader("...")]`-tagged
# entry point (VSMain, PSMain, CSMain, raygen, etc.). The pipeline picks
# entries by name at vkCreate*Pipelines time.
#
# Two usage modes:
#
# 1) Auto-discover (recommended) — globs every `.slang` under ROOT, recursively.
#    EXCLUDE_DIRS filters out import-only modules like Common/. New shaders
#    dropped into the tree are picked up by the next `cmake --build` (no
#    manual reconfigure needed, thanks to CONFIGURE_DEPENDS).
#
#       helio_compile_shaders(TARGET HelioShaders
#           ROOT          ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
#           EXCLUDE_DIRS  Common
#           INCLUDE_DIRS  ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Common
#           OUTPUT_DIR    ${CMAKE_BINARY_DIR}/Helio
#       )
#
# 2) Explicit SOURCES list — when you want strict control over what compiles.
#
#       helio_compile_shaders(TARGET HelioShaders
#           SOURCES
#               Shaders/Passes/Triangle.slang
#               Shaders/Passes/FullscreenBlit.slang
#           INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Common
#           OUTPUT_DIR   ${CMAKE_BINARY_DIR}/Helio
#       )
#
# The two modes can be combined — auto-discovered sources are merged with any
# explicit SOURCES list and de-duplicated.

find_program(SLANGC_EXECUTABLE
    NAMES slangc
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES Bin bin
    DOC "Slang compiler (bundled with Vulkan SDK)"
)

if(NOT SLANGC_EXECUTABLE)
    message(WARNING "slangc not found. Set VULKAN_SDK or install the Vulkan SDK. Shader compilation will be disabled.")
endif()

function(helio_compile_shaders)
    set(options)
    set(oneValueArgs TARGET OUTPUT_DIR ROOT)
    set(multiValueArgs SOURCES INCLUDE_DIRS EXCLUDE_DIRS)
    cmake_parse_arguments(HCS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT HCS_TARGET)
        message(FATAL_ERROR "helio_compile_shaders: TARGET is required")
    endif()
    if(NOT HCS_OUTPUT_DIR)
        set(HCS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
    endif()

    file(MAKE_DIRECTORY ${HCS_OUTPUT_DIR})

    # Auto-discover: when ROOT is given, glob every .slang under it and merge
    # into SOURCES. EXCLUDE_DIRS skips import-only modules (e.g. Common/) from
    # COMPILATION — but those files are still added to the IDE project so they
    # appear in Solution View / Project View.
    # CONFIGURE_DEPENDS makes the next `cmake --build` notice newly-added .slang
    # files automatically — no manual `cmake --preset` reconfigure needed.
    set(_ALL_DISCOVERED)
    if(HCS_ROOT)
        file(GLOB_RECURSE _DISCOVERED CONFIGURE_DEPENDS "${HCS_ROOT}/*.slang")
        # Keep the full list (including EXCLUDE_DIRS) for IDE visibility.
        set(_ALL_DISCOVERED ${_DISCOVERED})
        foreach(_EX IN LISTS HCS_EXCLUDE_DIRS)
            list(FILTER _DISCOVERED EXCLUDE REGEX "/${_EX}/")
        endforeach()
        foreach(_F IN LISTS _DISCOVERED)
            file(RELATIVE_PATH _REL ${CMAKE_CURRENT_SOURCE_DIR} ${_F})
            list(APPEND HCS_SOURCES ${_REL})
        endforeach()
        if(HCS_SOURCES)
            list(REMOVE_DUPLICATES HCS_SOURCES)
        endif()
    endif()

    set(INCLUDE_FLAGS)
    foreach(D IN LISTS HCS_INCLUDE_DIRS)
        list(APPEND INCLUDE_FLAGS -I ${D})
    endforeach()

    set(OUTPUTS)
    foreach(SRC IN LISTS HCS_SOURCES)
        get_filename_component(SRC_NAME ${SRC} NAME_WE)
        get_filename_component(SRC_DIR  ${SRC} DIRECTORY)
        set(OUT_DIR ${HCS_OUTPUT_DIR}/${SRC_DIR})
        set(OUT     ${OUT_DIR}/${SRC_NAME}.spv)
        file(MAKE_DIRECTORY ${OUT_DIR})

        if(SLANGC_EXECUTABLE)
            add_custom_command(
                OUTPUT ${OUT}
                COMMAND ${SLANGC_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
                    -target spirv
                    -profile sm_6_6
                    -capability spvRayTracingKHR
                    -capability spvRayQueryKHR
                    -fvk-use-entrypoint-name
                    -emit-spirv-directly
                    ${INCLUDE_FLAGS}
                    -o ${OUT}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
                COMMENT "slangc ${SRC} -> ${SRC_DIR}/${SRC_NAME}.spv"
                VERBATIM
            )
            list(APPEND OUTPUTS ${OUT})
        endif()
    endforeach()

    # Build a list of absolute paths for IDE visibility. Include EXCLUDE_DIRS
    # files (e.g. Common/*.slang) so import-only modules show up too.
    set(_IDE_SOURCES)
    if(_ALL_DISCOVERED)
        list(APPEND _IDE_SOURCES ${_ALL_DISCOVERED})
    endif()
    foreach(SRC IN LISTS HCS_SOURCES)
        if(IS_ABSOLUTE "${SRC}")
            list(APPEND _IDE_SOURCES "${SRC}")
        else()
            list(APPEND _IDE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
        endif()
    endforeach()
    if(_IDE_SOURCES)
        list(REMOVE_DUPLICATES _IDE_SOURCES)
    endif()

    # `add_custom_target(... SOURCES ...)` registers files with the IDE
    # without trying to compile them as C++. They show up in Visual Studio's
    # Solution Explorer / Rider's Solution View under the target's FOLDER.
    add_custom_target(${HCS_TARGET} ALL
        DEPENDS ${OUTPUTS}
        SOURCES ${_IDE_SOURCES}
    )
    set_target_properties(${HCS_TARGET} PROPERTIES FOLDER "Helio/Shaders")

    # Mirror the on-disk Shaders/ subfolder tree inside the IDE so Common/,
    # Passes/, RT/, Compute/, etc. each get their own group.
    if(HCS_ROOT AND _IDE_SOURCES)
        source_group(TREE "${HCS_ROOT}" PREFIX "Shaders" FILES ${_IDE_SOURCES})
    endif()

    # Tag every .slang as "header-only" (HEADER_FILE_ONLY) so MSBuild never
    # tries to invoke a C++ compiler on it. Without this, VS proper sometimes
    # treats unknown-extension files as build inputs and emits a warning.
    set_source_files_properties(${_IDE_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

    # Expose both the .spv output list and their common root dir so callers
    # (game/CMakeLists.txt) can build deploy steps that depend on individual
    # outputs — that way a shader-only change still triggers the copy without
    # needing Game.exe to relink.
    set_target_properties(${HCS_TARGET} PROPERTIES
        HELIO_SHADER_OUTPUT_DIR "${HCS_OUTPUT_DIR}"
        HELIO_SHADER_OUTPUTS    "${OUTPUTS}"
    )
endfunction()

