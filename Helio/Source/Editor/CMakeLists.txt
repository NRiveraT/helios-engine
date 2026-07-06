# Helio.Editor — Dear ImGui (docking) editor overlay.
# ImGui core + its SDL3 platform binding come from vcpkg; rendering is a
# from-scratch bindless backend on Helio.RHI (no imgui_impl_vulkan).

find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

helio_add_module(Editor
    SOURCES
        EditorOverlay.cpp
        ImGuiRenderer.cpp
    PUBLIC_LINK
        Helio.Core
        Helio.Scene
        Helio.Renderer
        Helio.RHI
        Helio.Platform.Windows
        Helio.Input
        imgui::imgui
    PRIVATE_LINK
        SDL3::SDL3
)
