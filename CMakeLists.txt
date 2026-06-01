cmake_minimum_required(VERSION 3.27)
project(Helio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Place built binaries side-by-side under build/<preset>/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /MP /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(HELIO_TRACY "Enable Tracy profiler" ON)
option(HELIO_OVERLAY "Enable on-screen overlay" ON)
option(HELIO_VALIDATION "Enable Vulkan validation layers" ON)

include(Helio/Tools/Build/EngineHelpers.cmake)
include(Helio/Tools/Build/CompileShaders.cmake)

add_subdirectory(Helio)
add_subdirectory(game)
