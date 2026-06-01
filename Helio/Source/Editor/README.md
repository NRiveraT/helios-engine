# Editor — placeholder

Not in V1. Reserved for a future custom editor (the user wants to build their own).

**V1's architectural readiness for the editor:**

- `Helio` is built as a static lib (see `Helio/CMakeLists.txt`), so a future editor executable can link it the same way `game/` does.
- `RenderGraph` can target arbitrary `Texture` handles (not just the swapchain). An editor viewport is just a render target; embedding the game render inside an editor window means binding the editor's viewport texture as the graph's output instead of the swapchain.
- Shader hot reload (Phase 13) means the editor can edit `.slang` files and see results live.
- Tracy (Phase 2) gives the editor out-of-process profiling that can later be embedded.
- Input dispatch (Phase 10) is pluggable: the editor can capture input before `game/` sees it.

No editor code is written in V1. This README is the entry point for whoever picks it up later.
