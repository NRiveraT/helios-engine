# Editor

Dear ImGui (docking) editor overlay, toggled with F1 at runtime.

- `EditorOverlay` — context + SDL3 platform backend, dockspace, Scene tree /
  Inspector / Stats panels, input capture via `Window::SetNativeEventHook`.
- `ImGuiRenderer` — from-scratch bindless render backend on `Helio.RHI`
  (`Shaders/Editor/ImGuiPass.slang`); no `imgui_impl_vulkan`.

Docs: [`Docs/Editor.md`](../../Docs/Editor.md). ImGui itself comes from vcpkg
(`imgui[docking-experimental, sdl3-binding]`). Future: gizmos, asset browser,
multi-viewport, play-in-editor state separation.
