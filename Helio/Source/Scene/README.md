# Scene

The world/actor layer — scene graph, cameras, lights, and the renderer that
draws a world (`SceneRenderer`, including the fitted directional shadow map).

- `HelioWorld` — owns actor lifetimes (flat storage), spawn/destroy/tick.
- `Actor` — scene-graph node: local transform + lazily-cached world transform,
  parent/child attachment, UE-style world/local delta API.
- `Actors/` — `Camera` (data-only), `DirectionalLight`, `StaticMeshActor`.
- `SceneRenderer` — batching, `FrameConstants`, shadow + mesh passes,
  overlay hook for the editor.

Docs: [`Docs/Scene.md`](../../Docs/Scene.md). This module is input-free by
design — camera controllers live in `Helio.Gameplay`. Future: ECS storage,
spatial partitioning, culling.
