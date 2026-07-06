# Scene — world, actors, scene graph, shadows

`Helio.Scene` (`helio::scene`) is the world/actor layer: `HelioWorld` owns actor
lifetimes, `Actor` carries the transform hierarchy, and `SceneRenderer` draws it
all — including the fitted directional shadow map.

## World & actors

```cpp
scene::HelioWorld World;

auto* Body = World.SpawnActorNamed<scene::StaticMeshActor>("Body", BodyMesh);
auto* Turret = World.SpawnActorNamed<scene::StaticMeshActor>("Body.Turret", TurretMesh);
Turret->AttachTo(*Body, scene::Actor::AttachRule::KeepLocal);
Turret->SetLocalPosition(float3(0, 0.8f, 0));

Body->AddWorldRotation(float3(0, 1, 0), YawDelta);   // turret follows for free

World.DestroyActor(*Body);   // deferred; destroys the whole subtree at end of Tick
World.Tick(Dt);              // ticks live actors, then flushes the destroy queue
```

Rules worth knowing:

- **Ownership is flat, hierarchy is topology.** The World owns every actor in a
  flat vector; `AttachTo`/`Detach` only rewire parent/child links. Destroying a
  parent destroys its subtree (children first, `EndPlay` post-order).
- **Local vs world.** `GetLocalTransform()` is relative to the parent (or the
  world when unparented). `GetWorldTransform()` is cached and recomposed lazily
  (`parentWorld * local`) — mutating any local component dirty-marks the whole
  subtree, so deep chains cost nothing until read.
- **UE-style deltas.** `AddWorldOffset`/`AddWorldRotation` operate on world
  axes; `AddLocalOffset`/`AddLocalRotation` on the actor's own axes.
- **Setters, not mutable refs.** There is no mutable `GetTransform()` — all
  mutation goes through setters so the world-transform cache can never go
  stale.
- **Non-uniform scale caveat** (same as UE's `FTransform`): `Transform`
  composition can't represent shear, so parent chains with non-uniform scale +
  rotation are approximated. Point maps (`TransformPoint` etc.) are always
  exact.

`Camera` is pure data: cached reverse-Z projection + view matrix derived as the
exact rigid inverse of its world transform (`Transform::ToViewMatrix`) —
camera controllers live in Gameplay (`FlyCameraController`), never in Scene.

`DirectionalLight`'s direction IS its forward axis; rotate the actor to aim the
sun. Color/intensity/ambient feed the shaders through `FrameConstants`.

## SceneRenderer frame

1. `Actor::OnRender(SceneRenderer&)` — renderable actors submit
   (`SubmitMesh(mesh, material, worldTransform)`); world-space caster bounds
   accumulate here.
2. Instances batch into one ring-buffered buffer (one `DrawIndexed` per mesh).
3. `FrameConstants` (view-proj, camera, light, shadow matrix + params) upload
   once into a bindless SSBO — see `Shaders/Common/Frame.slang`. Push constants
   carry only per-draw slots + material scalars.
4. Passes, in order: **Shadow Map** (depth-only) → **Static Meshes** (PBR +
   shadow sampling) → debug lines → stats overlay → overlay hook (editor) →
   present.

## Directional shadows

The sun shadow map (2048², D32) uses the same conventions as every other pass —
reverse-Z (`Greater`, clear 0.0) and a Y-negated ortho projection
(`math::OrthoReverseZLH`), so `FrontFace::Clockwise` applies uniformly and the
mesh pass samples with the *same* `LightViewProj` matrix that rendered the map.

Fitting (`SceneRenderer::BuildShadowData`):

- The casters' world AABB is wrapped in a **bounding sphere** — rotation
  invariant, so the ortho extent doesn't wobble as the light turns.
- The sphere center **snaps to shadow-texel increments in light space**,
  eliminating edge shimmer from sub-texel frustum translation.
- Near/far pancake the sphere with margins; depth is linear across the range.

Acne control is layered: slope-scaled rasterizer depth bias on the caster
pipeline (negative values — reverse-Z flips the sign), normal-offset sampling
(~1.5 texels along the receiver normal), and a small receiver-side depth bias.
Filtering is 3×3 hardware PCF via the comparison sampler at bindless slot
`kSamplerShadowLinear` (`GREATER_OR_EQUAL`; border black = lit outside the
frustum).

No casters → shadows disable for the frame (`ShadowParams.w = 0`) and the mesh
pass skips sampling entirely.

### Using shadows

Shadows are automatic — there is nothing to enable. Any `StaticMeshActor` in
the world casts and receives, and the first `DirectionalLight` in the world is
the sun. Aim the sun by rotating that actor (its forward axis is the light
travel direction):

```cpp
auto* Sun = World.SpawnActorNamed<scene::DirectionalLight>("Sun");
Sun->SetWorldRotation(QuatFromEuler(0.9f, 0.5f, 0.0f)); // pitch, yaw, roll (rad)
Sun->SetColor(float3(1.0f, 0.96f, 0.9f));
Sun->SetIntensity(3.0f);
Sun->SetAmbient(0.03f);   // flat fill so unlit faces aren't pure black
```

### Tuning / modifying shadows

Every knob lives in one of two places — the pipeline (created once in
`SceneRenderer`'s constructor) or the per-frame `FrameConstants` (filled in
`SceneRenderer::Render`). Change them there:

| Knob | Where | Notes |
|---|---|---|
| **Resolution** | `SceneRenderer::kShadowMapResolution` (header, default 2048) | Higher = crisper edges, quadratic VRAM/fill cost. Powers of two. |
| **Depth bias** (acne vs. peter-panning) | `m_ShadowMapPipeline` desc: `DepthBiasConstant = -2`, `DepthBiasSlope = -3` | **Negative** because reverse-Z flips the sign — negative pushes casters *away* from the light. Too little → acne (self-shadow stripes); too much → contact shadows detach. |
| **Normal-offset** | `FC.ShadowParams.y = Shadow.TexelWorldSize * 1.5f` in `Render()` | Samples the receiver as if lifted ~1.5 texels along its normal. The main acne fix on slopes; raise if stripes persist, lower if shadows detach. |
| **Receiver depth bias** | `ReceiverBiasNDC` in `Render()` (≈ 1 texel of NDC depth) | Small constant nudge toward the light in the compare. |
| **PCF kernel** | the `[-1,1]×[-1,1]` loop in `SampleSunShadow` (`Shaders/Common/Frame.slang`) | 3×3 taps × hardware 2×2 = ~6×6 smoothing. Widen the loop bounds for softer edges (more taps = more cost). |
| **Fit tightness** | `BuildShadowData` (`NearPad`, the sphere radius) | The frustum wraps a sphere around all casters. Tighter fit = more effective resolution but risk of clipping tall casters; the padding guards the near/far planes. |

To add a **second cascade** (CSM) later: `BuildShadowData` already fits *per
region*, so the path is a texture array + a per-cascade split of the camera
frustum + a cascade-select in `SampleSunShadow` — no rewrite of the fit math.
Spot / point shadows are a new pipeline (perspective projection, cube map for
point) but reuse the same depth-only pass structure.
