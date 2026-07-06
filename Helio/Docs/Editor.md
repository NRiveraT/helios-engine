# Editor — Dear ImGui docking overlay

`Helio.Editor` (`helio::editor`) draws a toggleable editor over the running
game. Press **F1** (key-repeat filtered, works regardless of gameplay
bindings).

## Panels

- **Scene** (left): the world's actor tree — parent/child nesting, click to
  select, right-click for *Delete (with children)* / *Detach from parent*.
- **Inspector** (right): name, local position, rotation (Euler degrees,
  cached while editing so pole round-trips don't jitter), scale — plus
  type-specific sections for `StaticMeshActor` (material), `DirectionalLight`
  (color/intensity/ambient) and `Camera` (FOV/near).
- **Stats** (bottom-left): CPU render ms, GPU frame ms, actor count.

The default dock layout builds on first run; afterwards it persists in
`EditorLayout.ini` next to the binary. The dockspace's central node is
passthrough — the 3D scene stays visible and interactive through the middle.

## Input routing

The overlay installs `Window::SetNativeEventHook` and feeds every SDL event to
ImGui's SDL3 backend while visible. Events are **consumed** (kept from gameplay)
when ImGui wants them: key-down / text on `io.WantCaptureKeyboard`, mouse
button-down / motion / wheel on `io.WantCaptureMouse`. **Releases are never
consumed** — a key/button whose release the UI swallowed would stay stuck
"held" in the gameplay dispatcher. Window-close is never consumable. On toggle,
the dispatcher's held-key state resets and the fly-cam mouse capture releases.

The fly camera coexists with the editor by the **hold-RMB-to-fly** convention
(same as Unreal/Unity scene view): look and WASD/EQ movement engage only while
right mouse is held over the 3D viewport. The engine gates `FlyCameraController`
on `EditorOverlay::WantsInput()`, which reports `WantCaptureMouse` **only** —
keyboard capture is deliberately excluded so a focused panel never disables the
camera mid-flight. `ImGuiConfigFlags_NavEnableKeyboard` is intentionally OFF for
the same reason (it would make ImGui claim the keyboard globally and eat WASD).

## Rendering

`ImGuiRenderer` is a from-scratch backend on Helio's bindless RHI — no
`imgui_impl_vulkan`. Vertices and u16 indices stream into per-frame
`RingUploadBuffer`s and are pulled by `SV_VertexID` in
`Shaders/Editor/ImGuiPass.slang` (one non-indexed draw + scissor per
`ImDrawCmd`, alpha blending via `BlendMode::Alpha`, vertex colors converted
sRGB→linear for the sRGB target). The font atlas uploads once through the
normal bindless texture path; its sampled slot doubles as the `ImTextureID`.
The pass is declared through `SceneRenderer::SetOverlayHook`, after all scene
passes and before present.

## Using / modifying the editor

**Toggle at runtime:** F1. Programmatically: `engine.Editor().SetVisible(true)`
or `.Toggle()`. `IsVisible()` / `WantsInput()` query state.

**Per-frame flow** (already wired in `HelioEngine::Run`, replicate if you write
your own loop): call `EditorOverlay::BeginFrame()` once per frame *after*
`Window::PumpEvents` and *before* `SceneRenderer::Render`. `BeginFrame` opens
the ImGui frame and builds all panels; the actual GPU pass is emitted later via
the SceneRenderer overlay hook. `BeginFrame` also self-heals a dropped frame
(if a previous frame's `ImGui::Render` never ran — e.g. the swapchain was
out-of-date during a resize — it closes the orphaned ImGui frame so the next
`NewFrame` doesn't assert).

**Add a panel:** write a `DrawMyPanel()` method on `EditorOverlay` that does
`ImGui::Begin("My Panel") … ImGui::End()`, and call it from `BeginFrame()`
alongside the existing `DrawScenePanel` / `DrawInspectorPanel` / `DrawStatsPanel`.
To dock it by default, add a `DockBuilderDockWindow("My Panel", <node>)` line in
`DrawDockspace()`'s first-run layout block (or just let the user drag it — the
layout persists in `EditorLayout.ini`).

**Add an inspector field:** the type-specific blocks in `DrawInspectorPanel` are
plain `dynamic_cast<T*>(Selected)` checks. Add a new `if (auto* X =
dynamic_cast<MyActor*>(Selected))` block and drive `ImGui::DragFloat` /
`ColorEdit3` / `Checkbox` against the actor's setters. Mesh materials are edited
per **section** (see `Docs/Scene.md` / `Meshes.md` on mesh sections) — one
collapsing header per section.

**Delete safety:** the Scene panel can destroy any actor (`World::DestroyActor`,
deferred to end of `World::Tick`). Anything the game holds across frames must be
re-resolved by id (`World::FindActorById`) each frame, never cached as a raw
pointer — see `HelioEngine::Run`. This is why deleting the camera or an animated
actor from the editor doesn't crash.

**Fonts / style:** the atlas is built once in the `ImGuiRenderer` constructor;
call `ImGui::GetIO().Fonts->AddFontFromFileTTF(...)` *before* constructing the
renderer (i.e. before `EditorOverlay`) if you want a custom font, then it uploads
through the normal bindless texture path automatically.
