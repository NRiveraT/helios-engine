# Meshes

GPU-resident static meshes — load via procedural primitives (or glTF, deferred), upload through `MeshSystem`, draw via `InstanceBatch` + `MeshInstancedPipeline`. Bindless vertex pull, optional meshopt optimization, automatic u16/u32 index packing, AABB bounds, hot-swappable per-frame instance counts.

## API at a glance

```cpp
#include <Core/Math/Math.h>
#include <Resource/Mesh.h>
#include <Resource/MeshPrimitives.h>
#include <Resource/MeshPipeline.h>
#include <Resource/InstanceBatch.h>
```

| Type | Lives in | Use for |
|---|---|---|
| `Transform` | `Core/Math/Transform.h` | game-side state: position + quaternion + scale, mutable over time |
| `Vertex` | `Resource/MeshData.h` | 48-byte interleaved vertex (matches `Shaders/Common/Vertex.slang`) |
| `MeshData` | `Resource/MeshData.h` | CPU-side authored mesh (vertices + indices) |
| `primitives::Cube/Sphere/Plane/Cylinder` | `Resource/MeshPrimitives.h` | procedural mesh data |
| `MeshSystem` | `Resource/Mesh.h` | owns GPU mesh storage; create at boot |
| `Mesh` | `Resource/Mesh.h` | GPU handle (POD; copy by value) |
| `IndexTypeFor(Mesh)` | `Resource/Mesh.h` | picks `IndexType::U16` / `U32` for `BindIndexBuffer` |
| `InstanceBatch` | `Resource/InstanceBatch.h` | per-frame transform ring buffer (accepts `Transform` or `MeshInstance`) |
| `MeshInstance` | `Resource/InstanceBatch.h` | low-level 64-byte per-instance GPU payload (you usually won't touch this) |
| `CreateMeshInstancedPipeline()` | `Resource/MeshPipeline.h` | one-call factory for the canonical 3D mesh pipeline |
| `MeshInstancedPushConsts` | `Resource/MeshPipeline.h` | 112-byte push-constant struct (assignable fields) |

## Quick "I want to see it" recipe (single cube)

```cpp
using namespace helio;
using namespace helio::rhi;
using namespace helio::resource;
using namespace helio::render;
using namespace hlslpp;

// --- setup (once) ---------------------------------------------------------
Device RHI(...);
MeshSystem MS(RHI);

auto CubeData = primitives::Cube(1.0f);
auto Cube     = MS.CreateMesh({.Data = &CubeData, .DebugName = "Cube"});

auto MeshPipe = CreateMeshInstancedPipeline(RHI, {
    .ColorFormat = Format::RGBA8_SRGB,
    .DepthFormat = Format::D32_SFLOAT,
});

InstanceBatch Batch(RHI, /*MaxInstances=*/1, "CubeInstances");

auto Depth = RHI.CreateTexture({
    .Width = uint32_t(Win.Width()), .Height = uint32_t(Win.Height()),
    .Fmt = Format::D32_SFLOAT,
    .Usage = TextureUsage::DepthStencilAttachment,
    .DebugName = "Depth",
});

// --- per frame -----------------------------------------------------------
Transform T;
T.Position = float3(0, 0, -3);                  // 3m in front
Batch.Begin();
Batch.Add(T);                                   // converts to matrix internally
uint32_t Count = Batch.End();

rg.Graphics("Meshes")
  .Color(Color, 0.05f, 0.06f, 0.08f, 1.0f)
  .Depth(Depth, /*ClearDepth=*/0.0f)             // reverse-Z: clear FAR to 0
  .Execute([&](CommandList& C) {
      const float Aspect = float(Win.Width()) / float(Win.Height());
      float4x4 Proj = math::PerspectiveReverseZLH(hlslpp::radians(45.0f), Aspect, 0.01f);
      float4x4 View = math::LookAtLH(float3(0, 1, 3), float3(0, 0, 0), float3(0, 1, 0));

      MeshInstancedPushConsts PC{};
      PC.ViewProj           = mul(Proj, View);      // mul(), not *
      PC.VertexBufferSlot   = Cube.VertexBuffer.BindlessSlot;
      PC.InstanceBufferSlot = Batch.Buffer().BindlessSlot;
      PC.LightDirWS         = float3(-0.5f, -0.8f, -0.3f);
      PC.AlbedoTint         = float4(1, 1, 1, 1);

      C.Bind(MeshPipe);
      C.SetViewportFull();
      C.Push(PC);
      C.BindIndexBuffer(Cube.IndexBuffer, IndexTypeFor(Cube));
      C.DrawIndexed(Cube.IndexCount, Count);
  });
```

Expected result: a white cube facing the camera, lit from above-right-front.

## Recipe: 10×10 grid of cubes (instanced)

Same setup, just bump capacity and rebuild the batch each frame:

```cpp
InstanceBatch Batch(RHI, /*MaxInstances=*/100, "CubeGrid");

// per frame:
Batch.Begin();
for (int z = 0; z < 10; ++z) {
    for (int x = 0; x < 10; ++x) {
        Transform T;
        T.Position = float3(float(x*2 - 9), 0.0f, float(z*2 - 9));
        Batch.Add(T);
    }
}
uint32_t Count = Batch.End();
// ... bind + push + draw same as single-cube — `Count` is now 100.
```

One draw call, 100 instances, ~12 triangles each.

## Recipe: rotating cube using delta time

The `Transform` is a normal mutable game-state type — modify it across frames using `Dt`:

```cpp
Transform Cube;
Cube.Position = float3(0, 0, -3);

core::Clock GameClock;
while (Win.PumpEvents()) {
    const float Dt = float(GameClock.Tick());

    // Spin 1.5 rad/sec around world Y, in place:
    Cube.RotateAxis(float3(0, 1, 0), 1.5f * Dt);

    // Optional: orbit around the origin (vary position by Dt):
    // Cube.Translate(float3(std::cos(...) * Dt, 0, std::sin(...) * Dt));

    Batch.Begin();
    Batch.Add(Cube);
    uint32_t Count = Batch.End();
    // ... draw as before.
}
```

The conversion to a GPU matrix happens inside `Batch.Add(Cube)` via `Transform::ToMatrix()` — your loop never sees a `Mat4Packed` or a `mul()` call.

## `Transform` is a Core type, not a mesh type

`helio::Transform` lives in `Core/Math/Transform.h` because transforms belong far higher up the chain than the rendering layer — cameras, lights, audio emitters, AI agents, physics bodies all hold a transform. The `MeshInstance` GPU payload (a packed 4×4 matrix) is just one *consumer* of a `Transform`; game code stays in (`Position`, `Rotation`, `Scale`) form and converts only at upload time.

```cpp
Transform T;
T.Position = float3(0, 5, 0);
T.RotateAxis(float3(0, 1, 0), 0.5f);   // 0.5 rad about world Y
T.ScaleBy(2.0f);                        // uniform 2x

// Same Transform feeds different consumers:
Batch.Add(T);                          // GPU mesh instance
camera.LookFrom(T);                    // future: camera component
physics.SetPose(T);                    // future: rigid body component
```

The class is intentionally simple — it doesn't know about scene hierarchies, dirty flags, or parent pointers. Those land in the future component / scene layer; `Transform` is the leaf representation.

### Composition

`parent * child` composes two transforms (`parent.ToMatrix() * child.ToMatrix()` is equivalent but slower because it round-trips through matrices):

```cpp
Transform World = ParentBone * LocalOffset;
```

### Quaternion helpers

Free functions in `helio::`:
- `QuatFromAxisAngle(axis, radians)` — most common axis rotation
- `QuatFromEuler(pitch, yaw, roll)` — intrinsic Tait-Bryan (Yaw→Pitch→Roll)
- `QuatMul(a, b)` — Hamilton product (applies `b` first, then `a`)
- `QuatSlerp(a, b, t)` — shortest-arc interpolation
- `QuatNormalize(q)`, `QuatConjugate(q)`, `QuatToMatrix(q)`

## Single mesh vs. instanced — same path, different sugar

Helio does **not** distinguish between "single mesh draw" and "instanced batch draw" at the GPU level — both go through `DrawIndexed(IndexCount, InstanceCount)`. The `InstanceCount` is just `1` for the single-mesh case.

The cost of using `InstanceBatch` for a single mesh is **one 64-byte per-frame upload** (the single `MeshInstance` payload) — negligible. The benefit is uniform draw code: when you later wrap this in a `StaticMeshComponent` vs an `InstancedStaticMeshComponent`, both call the same `DrawIndexed` path:

```cpp
// hypothetical future component layer:
class StaticMeshComponent {
    Mesh        m_mesh;
    float4x4    m_transform;
    void Render(Renderer& R) {
        R.SubmitMesh(m_mesh, std::span{&m_transform, 1});   // InstanceCount = 1
    }
};

class InstancedStaticMeshComponent {
    Mesh                     m_mesh;
    std::vector<float4x4>    m_transforms;
    void Render(Renderer& R) {
        R.SubmitMesh(m_mesh, m_transforms);                 // InstanceCount = N
    }
};
```

Future GPU-driven culling (indirect draws + visibility buffer) is also a natural addition on top of this same path — `vkCmdDrawIndexedIndirect` reads the same instance buffer with a culling-shader-generated count.

## Delta time

`helio::core::Clock::Tick()` returns the seconds elapsed since the previous `Tick()` call (zero on the first call). Use a dedicated clock for delta time and keep it separate from any clock you use for profiling:

```cpp
helio::core::Clock GameClock;        // for game-state delta time
helio::core::Clock FrameClock;       // for CPU-ms measurement (existing pattern)

while (Win.PumpEvents()) {
    const float Dt = float(GameClock.Tick());            // seconds since last frame
    const double FrameStart = FrameClock.SecondsSinceStart();

    // update game using Dt
    Object.Position += Velocity * Dt;
    Spinner.Angle   += 1.5f * Dt;                        // 1.5 rad/sec

    // render (uses the matrices you computed above)
    ...

    // (optional) measure this frame's CPU time:
    const double CpuMs = (FrameClock.SecondsSinceStart() - FrameStart) * 1000.0;
    Hud.DrawStats(CpuMs, RHI.LastFrameGpuMs(), rg.Passes());
}
```

Typical patterns:

```cpp
// Spin a cube around Y at 90°/sec:
I.Transform = mul(math::Translation(0, 0, -3),
                  math::RotationY(TotalTimeSec * 1.57f));   // π/2 rad/sec

// Time-of-day light:
PC.LightDirWS = float3(std::cos(TotalTimeSec), -0.7f, std::sin(TotalTimeSec));
```

(`TotalTimeSec` is `GameClock.SecondsSinceStart()` — useful when you want a continuous angle rather than an integrated delta.)

## Pipeline conventions baked in

These get repeated across docs because they all interact with the mesh path — collected here for reference:

| Convention | Where set | Why |
|---|---|---|
| Column-vector math | `Core/Math/Math.h` | textbook form; `mul(Proj, View)` reads naturally |
| 48-byte interleaved Vertex | `MeshData.h` / `Vertex.slang` | one bindless slot per mesh, cache-friendly for forward shading |
| Y negated in projection | `PerspectiveReverseZLH` | gives world-up = screen-up under Vulkan's Y-down framebuffer |
| `FrontFace::Clockwise` on 3D pipelines | `MeshInstancedPipelineDesc` default | undoes winding-sign flip caused by the projection Y negation |
| `CullMode::Back` | pipeline desc default | culls camera-facing-away triangles |
| Reverse-Z depth | `CompareOp::Greater` default | depth precision concentrated near the camera |
| Depth clear to `0.0` | pass declaration | reverse-Z: `0.0` is the far plane |

## What's wired up

- **meshopt** integrated in `CreateMesh`: vertex-cache → overdraw (1.05 threshold) → vertex-fetch reorder
- **Automatic u16/u32 indices**: u16 when `VertexCount < 65536` (50% index-buffer VRAM savings)
- **Vertex remap** prefix-pass removes duplicate vertices before optimization
- **Bindless vertex pull**: vertex shader loads attributes by `SV_VertexID` from the storage-buffer slot
- **`RingUploadBuffer`-backed instance data** — race-safe across frames-in-flight
- **AABB bounds** auto-computed (or honored if supplied)
- **Build stats** captured per mesh: vertex count, triangle count, optimization flag, build time
- **Procedural primitives** with correct outward normals + glTF-convention tangent W-sign
- **`MeshInstancedPushConsts`** with `Mat4Packed` / `Vec3Packed` / `Vec4Packed` ergonomic assignment

## What's deferred

- **glTF loader** via fastgltf (planned next phase: `LoadGltfFirstPrimitive` / `LoadGltfAllPrimitives`)
- **Tangent generation** when source lacks them (`meshopt_generateTangents`)
- **Material slots** in `MeshInstance` (currently just a transform)
- **LOD chain** via `meshopt_simplify`
- **Meshlet generation** via `meshopt_buildMeshlets` (for cluster culling / mesh shaders)
- **SDF generation** (compute baker, post-V1)
- **`.helmesh` cached format** with `meshopt_encodeVertexBuffer` compression (~50% size)
- **Indirect draw / GPU culling** path (`vkCmdDrawIndexedIndirect`)
- **Auto-BLAS hook** (V1 has BLAS in the RHI but doesn't auto-wire from `CreateMesh`)
- **Non-uniform-scale-safe normals** (current shader assumes uniform scale; would want inverse-transpose for skewed transforms)

## Gotchas

- **`hlslpp` matrix `*` is component-wise**, not multiplication. Use `mul(A, B)`. This trips literally everyone the first time.
- **Pipeline + pass attachment formats must match**, even if a pipeline doesn't read/write a given attachment. If you bind a depth-less pipeline in a pass that has a depth attachment, validation rejects it. Either add matching `DepthFormat` + `DepthTest=false` to the pipeline, or split into separate passes.
- **`Cull::Back` requires `FrontFace::Clockwise`** on pipelines that use `PerspectiveReverseZLH` (the projection's Y-flip inverts the winding sign). The mesh pipeline factory sets this by default; custom pipelines via `Device::CreateGraphicsPipeline` need it set manually.
- **Push-constant struct padding follows HLSL packing rules**: `float3` can't straddle a 16-byte boundary, so Slang inserts padding bytes the C++ struct must mirror exactly. `MeshInstancedPushConsts` shows the pattern (`_PadA[2]` and `_Pad0`).
- **`InstanceBatch.Buffer()` rotates per frame** — re-query the bindless slot each frame in your push-constant fill. (The ring buffer hands you a different slot on rotating frame-in-flight indices.)
- **The mesh shader is a verification path, not a shading model**. `MeshInstanced.slang` does cheap N·L with one directional light — sufficient to see geometry, not to ship with. Build your own material pipelines on top using `import Vertex` from `Shaders/Common/`.
- **glTF tangents convention**: `Tangent.w` is the bitangent sign (`+1` or `-1`). The shader reconstructs bitangent as `cross(N, T.xyz) * T.w`. Procedural primitives bake `+1`; glTF-loaded meshes carry the source value.

## See also

- [Bindless.md](Bindless.md) — slot allocation and the bindless layout the mesh pipeline relies on
- [RenderGraph.md](RenderGraph.md) — pass declaration, attachment ordering, the `.Color()` vs `.ColorLoad()` distinction
- [Shaders.md](Shaders.md) — Slang language, `import` semantics, hot-reload (planned)
- [RT.md](RT.md) — BLAS/TLAS workflow (V1 doesn't auto-build BLAS from Mesh; do it manually via `Device::BuildBLAS`)
