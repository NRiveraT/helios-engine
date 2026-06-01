# Raytracing

Vulkan ray tracing in Helio uses a two-level acceleration-structure hierarchy + Slang's `RayQuery` for inline traces from any shader stage. The full RT pipeline path (raygen / miss / closest-hit + SBT) is Phase 13 polish; V1's RT story is **build BLAS/TLAS + ray query from compute or fragment**, which covers the bulk of practical RT use (shadows, AO, simple reflections).

All RT operations require `Device::HasRayTracing()` to return `true`. The engine falls back to a non-RT device gracefully — if a player's GPU can't satisfy the RT extension set, RT-tagged code paths in `game/` should check `HasRayTracing()` and skip.

## Concept

```
TLAS  ── one per scene, rebuilt per frame
 │
 ├── TLASInstance { BLAS, transform, mask, customIndex }
 ├── TLASInstance { ... }
 └── ...

BLAS  ── one per mesh, built once at load
 │
 └── BLASGeometry { vertex buffer, index buffer, vertex format }
```

- **BLAS** (bottom-level): geometry container. Building is expensive (driver constructs an acceleration tree); cheap to instance many times. Typically built once per static mesh at load time.
- **TLAS** (top-level): array of `BLAS` instances with per-instance transform + custom data. Building is cheap (just bounding-box reorg). Typically rebuilt every frame.

## Wiring a single triangle (verification path)

The fastest way to verify the whole chain works — no asset loader needed:

```cpp
// Once at startup:
auto Tlas = RHI.BuildVerificationTLAS();
// → builds a 3-vertex BLAS at world origin (unit XY triangle at z=0)
// → builds a 1-instance TLAS containing that BLAS at identity
// → calls SetActiveTLAS, so bindless slot 4 now points at it
```

That's it. Any shader can now `import Bindless;` and call `GetTLAS()`.

## Building your own BLAS + TLAS

```cpp
// One-time per static mesh.
BLASGeometry Geom{};
Geom.Vertices       = myMesh.VertexBuffer;    // BufferUsage::AccelStructureBuild
Geom.VertexStride   = sizeof(MyVertex);        // bytes per vertex
Geom.VertexCount    = myMesh.VertexCount;
Geom.VertexFormat   = Format::RGB32F;          // position layout at offset 0 in vertex
Geom.Indices        = myMesh.IndexBuffer;
Geom.IndexFormat    = 1;                       // 0 = U16, 1 = U32
Geom.IndexCount     = myMesh.IndexCount;
Geom.Opaque         = true;

BLASDesc BDesc{};
BDesc.Geometries     = &Geom;
BDesc.GeometryCount  = 1;
BDesc.PreferFastTrace = true;
BDesc.DebugName      = "MyMesh.BLAS";
BLASHandle Blas = RHI.BuildBLAS(BDesc);
```

```cpp
// Per frame — rebuild from current visible instances.
std::vector<TLASInstance> InstancesThisFrame;
for (const auto& Obj : VisibleObjects) {
    TLASInstance Inst{};
    Inst.BLAS = Obj.Mesh.Blas;
    std::memcpy(Inst.Transform, &Obj.World3x4, sizeof(Inst.Transform));
    Inst.Mask = Obj.RaytracingMask;       // 0xFF = traced by all rays
    Inst.CustomIndex = Obj.MaterialId;    // read in shader via CommittedInstanceID()
    InstancesThisFrame.push_back(Inst);
}

TLASDesc TDesc{};
TDesc.Instances     = InstancesThisFrame.data();
TDesc.InstanceCount = static_cast<uint32_t>(InstancesThisFrame.size());
TDesc.DebugName     = "Scene.TLAS";
TLASHandle NewTlas = RHI.BuildTLAS(TDesc);

// Old TLAS goes to the deletion queue when destroyed; the GPU finishes
// reading it via the active frame's command buffer before slots return.
RHI.DestroyTLAS(LastFrameTlas);
RHI.SetActiveTLAS(NewTlas);
LastFrameTlas = NewTlas;
```

Notes:
- Vertex buffers MUST be created with `BufferUsage::AccelStructureBuild` so they get the right Vulkan usage flags (acceleration-structure-build-input + shader-device-address). Index buffers the same.
- Vertex stride can be larger than `sizeof(float)*3` for interleaved layouts — the BLAS only reads the position at offset 0.
- BLAS storage is allocated automatically; the API hides scratch buffer management.

## Ray-query usage from Slang

```hlsl
import Bindless;

[[vk::push_constant]] struct PC { uint OutputSlot; } pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void CSMain(uint3 TID : SV_DispatchThreadID) {
    RWTexture2D<float4> Out = GetStorageImage(pc.OutputSlot);
    uint W, H; Out.GetDimensions(W, H);
    if (TID.x >= W || TID.y >= H) return;

    float2 UV = (float2(TID.xy) + 0.5) / float2(W, H);
    float2 NDC = UV * 2.0 - 1.0;

    RayDesc Ray;
    Ray.Origin    = float3(NDC.x, NDC.y, -1.0);
    Ray.Direction = float3(0, 0, 1);
    Ray.TMin = 0.001;
    Ray.TMax = 100.0;

    RayQuery<RAY_FLAG_NONE> Q;
    Q.TraceRayInline(GetTLAS(), 0u, 0xFFu, Ray);
    Q.Proceed();

    Out[TID.xy] = (Q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1);
}
```

A reference version of this lives at [`Helio/Shaders/RT/RayQueryTest.slang`](../Shaders/RT/RayQueryTest.slang) — use it to verify your `BuildVerificationTLAS()` setup before adding real geometry.

## Calling the test from C++

```cpp
// Once:
auto Tlas = RHI.BuildVerificationTLAS();
auto RtImage = RHI.CreateTexture({
    .Width = 1920, .Height = 1080,
    .Fmt = Format::RGBA8_UNORM,
    .Usage = TextureUsage::Storage | TextureUsage::Sampled,
    .DebugName = "RTOut",
});
auto RtPipe = RHI.CreateComputePipeline({
    .ShaderPath = "Shaders/RT/RayQueryTest.spv",
    .DebugName = "RayQueryTest",
});

// Per frame:
Cmd->TransitionForStorageWrite(RtImage);   // GENERAL layout for compute write
Cmd->Bind(RtPipe);
struct PC { uint32_t OutputSlot; } pc{ RtImage.StorageSlot };
Cmd->Push(pc);
Cmd->Dispatch2D(1920, 1080, 8, 8);

Cmd->TransitionForSampling(RtImage);       // back to SAMPLED for fragment read
// ... bind a fullscreen-blit pipeline that samples RtImage and draw to swapchain
```

You'll see a white triangle shape (the verification BLAS) on a black background — proving the entire chain: vertex buffer upload → BLAS build → TLAS build → bindless slot 4 → ray query → storage image write → sample → swapchain.

## Common gotchas

- **`SetActiveTLAS` not called.** The bindless TLAS slot starts unbound; ray queries against an unbound TLAS return miss for every ray.
- **BLAS storage type filter.** If the validation layer says "missing acceleration-structure-storage usage", the buffer wasn't created with the right `BufferUsage` flag — must include `AccelStructureBuild` for vertex/index inputs.
- **Old TLAS lifetime.** When you rebuild a TLAS per-frame, the *previous* TLAS is still being read by frames-in-flight. `RHI.DestroyTLAS(old)` puts it on the deletion queue so the GPU finishes before the actual `vkDestroyAccelerationStructureKHR`. Don't call `vkDestroy*` yourself.
- **Vertex format.** The BLAS reads only the position attribute at offset 0 of each vertex. For interleaved meshes, ensure position is the FIRST member of your vertex struct (or use a separate position-only buffer for BLAS input).
- **RT-mask culling.** `TLASInstance::Mask` is AND'd against the ray's mask. Common pattern: shadow rays use mask `0x01`, primary rays use `0xFF`. Set `Mask = 0x01` on glass / foliage instances that shouldn't shadow.

## What's deferred to Phase 13

- **Full RT pipeline + SBT** — raygen / miss / closest-hit / any-hit shader stages with VK_KHR_ray_tracing_pipeline. Useful for path tracing and complex hit-shader logic. V1's ray-query approach covers shadows, AO, simple reflections — most of what an engine actually needs.
- **Async BLAS builds** — V1 builds are synchronous (immediate submit + fence wait). Phase 13 lands batched builds via the render graph.
- **BLAS compaction** — post-build query of the actual used size + rebuild into a smaller storage buffer. Memory win for large scenes; not critical for verification.
