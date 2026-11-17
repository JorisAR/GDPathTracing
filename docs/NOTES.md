# Renderer modernization notes

Notes on the rework that brought the plugin to Godot 4.7. Phases 0–3 (toolchain, pipeline,
lighting, dynamic scenes) came first; then the hardware ray tracing backend, temporal
accumulation and portals. A spatial denoiser and the portal puzzle game are still to do (see the end).

## TL;DR

| | Before | After |
|---|---|---|
| Godot / bindings | 4.3, godot-cpp `4.3` + GDCS submodule | 4.7, godot-cpp `10.0.0-stable` (`api_version=4.7`), no GDCS |
| Presentation | Local RenderingDevice → `texture_get_data` (GPU→CPU) → `Image` → `ImageTexture` (CPU→GPU) → `TextureRect`, every frame | `CompositorEffect` writing straight into the scene's HDR color buffer on the main RenderingDevice |
| Color precision | `rgba8` radiance, clipped at 1.0 before accumulation | `rgba16f` output, `rgba32f` accumulation, Godot's tonemapper/exposure/glow apply |
| Workgroups | 32×32 (1024 threads, poor occupancy) | 8×8 |
| Lighting | BSDF sampling only | NEE on emissive triangles (power-proportional CDF) + MIS, plus directional/omni/spot lights with soft shadows |
| Scene | Built once, static | Instance transforms/visibility/lights tracked every frame, TLAS rebuilt on change |
| Tracing | Software BVH only | Hardware RT pipeline (RT cores) when available, software BVH as fallback |
| Motion | Accumulation reset on any change | Temporal reprojection with per-instance motion and history rejection |
| Portals | — | `Portal3D` node: rays and the fly camera pass through linked portals |

Measured on an RTX 4070 Ti SUPER, GPU time of the whole viewport (including Godot's own raster
pass and UI), from `capture.gd`:

| Scene | Software (compute BVH) | Hardware (RT pipeline) |
|---|---|---|
| Cornell demo, 1080p, 5 bounces, animated, temporal on | 7.9 ms | 3.5 ms |
| Cornell demo, 1152×648, animated | 3.1 ms | 1.3 ms (1.05 ms without temporal) |
| Lights demo (5k tris, 3 punctual lights + emissive), 1080p, static | 3.0 ms | 2.0 ms |
| Portal demo, 1152×648, static | 2.1 ms | 1.5 ms |

Both backends converge to the same image (mean difference < 0.05/255 on the Cornell and lights
demos).

I did not benchmark the old build (it needed the old GDCS + godot-cpp 4.3 toolchain), so there is
no exact before/after number. The old per-frame GPU→CPU→GPU round trip alone typically costs
several milliseconds at 1080p and forces a GPU sync.

## Phase 0: toolchain

- `godot-cpp` submodule moved to tag `10.0.0-stable`. godot-cpp 10 ships API files for 4.3–4.7;
  `SConstruct` defaults `api_version=4.7` (needed for the 4.7-only RenderingDevice ray tracing API
  we will use in phase 4). The stale root `extension_api.json` was removed.
- GDCS removed. Its job is now `src/rd/rd_util.*`: a growable `GpuBuffer` and a `make_uniform`
  helper. Uniform sets go through Godot's `UniformSetCacheRD`, so there is nothing to free by hand.
- `project/project.godot` is now tracked. It was previously ignored by `*.godot` in the root
  `.gitignore`, so a fresh clone could not open the demo. It defines the input actions the demo
  scripts use.
- `jar_path_tracing.gdextension` now requires 4.7.
- Removed leftovers: `jar_voxel_terrain.gd`, a `.tmp` scene, `icons.kra~`, the old
  `post_processing/` (progressive + temporal reprojection) and `render_parameters.h`/`utils.h`/`vec.h`.

Build:

```bash
scons target=template_debug platform=windows -j16
scons target=template_release platform=windows -j16
```

## Phase 1: rendering pipeline

`PathTracingCamera` now **extends `Camera3D`**. On `_ready` it creates a `Compositor` (or reuses
the one assigned) and appends a `PathTracingEffect`. The effect runs at
`EFFECT_CALLBACK_TYPE_POST_TRANSPARENT` on the render thread:

1. Takes a snapshot of the scene data published by `GeometryGroup3D` (mutex + versions, see
   `scene_data.h`) and re-uploads only what changed.
2. Gets ping-pong `rgba32f` history and G-buffer textures from `RenderSceneBuffersRD` (context
   `jar_path_tracing`). They follow viewport resizes automatically.
3. Builds the camera ray setup from `RenderSceneData::get_cam_transform()` and
   `get_cam_projection()`. That projection is reversed-Z and already contains Vulkan's y flip, so
   NDC y increases downwards in the shader.
4. Dispatches `main.glsl` (8×8 groups) or traces rays with `main_hw.glsl`; both trace,
   accumulate and write linear HDR radiance into the color buffer. Godot's tonemapping etc. happen afterwards, so the
   `WorldEnvironment` tonemap settings now control the look.

Accumulation resets on camera, projection, settings, scene or resolution changes, or when
`reset_accumulation()` is called. With `max_samples > 0` it stops tracing and just copies the
accumulated image each frame.

Camera properties: `max_bounces`, `samples_per_pixel`, `accumulate`, `max_samples`,
`firefly_clamp`, `debug_view` (None / Normals / Albedo / Traversal cost), sky gradient colors +
`sky_energy`, `path_tracing_enabled`.

Things to know:

- Godot still rasterizes the scene underneath, then we overwrite it (cost not measured separately;
  small for these demos). For heavy scenes, put the meshes on a render layer the camera's
  `cull_mask` excludes (the path tracer does not look at layers).
- Disable TAA/FSR on the viewport: they would reproject our image with the raster's motion vectors.
- Only Forward+ (needs RenderingDevice and an `rgba16f` color buffer). XR multiview is not handled
  (view 0 only). Orthographic cameras are not handled.
- The effect only runs at runtime, not in the editor viewport.

### Shader layout

```
shaders/
  main.glsl        software entry point (compute): includes common, brdfs, trace_sw, integrator
  main_hw.glsl     hardware entry point: #[raygen] (same includes with trace_hw), #[miss], #[closest_hit]
  integrator.glsli camera rays, path tracing loop, NEE + MIS, portals, temporal accumulation
  common.glsli     structs/bindings (mirror of gpu_types.h + FrameData), RNG, helpers
  brdfs.glsli      Burley-ish diffuse + GGX (VNDF sampling), ShadingInfo
  trace_sw.glsli   software two-level BVH traversal: trace_closest() / trace_any()
  trace_hw.glsli   the same two functions with traceRayEXT
  hw_payload.glsli ray payload shared by the raygen/miss/closest-hit stages
```

Includes use `.glsli` so Godot does not try to import them as standalone shaders. Two importer
gotchas:

- Godot's shader importer does not resolve nested `#include`s, so every include is listed in the
  entry files (in order).
- Godot only reimports `main.glsl`/`main_hw.glsl` when *those* files change, not when an included
  `.glsli` changes. After editing an include, touch the entry file or delete
  `.godot/imported/main*.glsl-*`; otherwise the old SPIR-V keeps running.

## Phase 2: lighting

- **Emissive triangles**: every triangle of a visible instance whose surface material has
  emission > 0. Power = luminance(emission) × world area, sampled through a CDF with binary search.
  Emitters are two-sided. Hits on emitters found by BSDF sampling are weighted with the power
  heuristic against the light-sampling pdf. The pdf is recomputed in the shader from the hit
  triangle's world area, so no triangle→light lookup table is needed.
- **Punctual lights** (`DirectionalLight3D`, `OmniLight3D`, `SpotLight3D` under the
  `GeometryGroup3D`): each is sampled every bounce with a shadow ray. Soft shadows come from
  `light_angular_distance` (sun) and `light_size` (omni/spot). Omni/spot falloff uses Godot's
  attenuation formula and `*_attenuation` exponent. Light energy is multiplied by π like Godot
  does, so a scene looks roughly the same as the raster version. The spot cone edge is a fixed
  10% smoothstep, which ignores `spot_angle_attenuation`.
- Shading fixes: normals use the inverse transpose (correct under non-uniform scale); the
  geometric normal is used for ray offsets and back-face flipping; sRGB→linear for albedo and
  emission colors (and an `R8G8B8A8_SRGB` texture array); Godot perceptual roughness is squared
  into GGX alpha; `uv1_scale`/`uv1_offset` are respected; Russian roulette after bounce 2; NaN/Inf
  guards; optional firefly clamp for indirect light.
- Materials: any `BaseMaterial3D` (Standard and ORM). `ShaderMaterial` falls back to the group's
  `default_material`. Each instance gets a slice of a material table indexed by surface, so the
  old limit of 3 surfaces per mesh is gone, and mesh materials (not only overrides) are used via
  `get_active_material()`.

## Phase 3: dynamic scenes

- `GeometryGroup3D` builds BLASes once per unique mesh (binned SAH, 16 bins, compact 32-byte
  nodes with sibling children, depth capped at 32 to bound the shader stack) on `_ready`.
- Every frame (internal process, priority 1000, so after gameplay scripts) it re-reads instance
  transforms and `is_visible_in_tree()`. If anything changed it rebuilds the instance buffer, the
  TLAS (agglomerative clustering, 32-bit child indices; the old version packed them into 16 bits)
  and the emissive CDF. Lights are compared byte-for-byte each frame. Nothing is uploaded when
  nothing moved.
- Adding/removing meshes or changing materials at runtime still needs `build()` (exposed to
  GDScript). That is a full rebuild; fine for loading, too slow for per-frame use.
- Hidden instances stay in the instance buffer but leave the TLAS, so indices stay stable.

Bugs fixed along the way: `BoundingBox` initialised `max` with `numeric_limits::min()` (a tiny
positive number); instance AABBs were multiplied by 2; `SurfaceTool` conversion only kept the last
surface; triangles were never cleared between builds; non-indexed meshes crashed; the missing
`break` after `NOTIFICATION_READY`; temporal reprojection compared against the current frame's
depth.

## Phase 4: hardware ray tracing

On this machine Godot 4.7.2 reports `SUPPORTS_RAY_QUERY = false` but
`SUPPORTS_RAYTRACING_PIPELINE = true`, so the hardware path uses the ray tracing pipeline:

- `main_hw.glsl` holds three stages. The raygen stage runs the same integrator as the compute
  shader, with `trace_hw.glsli` providing `trace_closest()`/`trace_any()` via `traceRayEXT`.
  Closest-hit only records `t`, barycentrics, `gl_InstanceCustomIndexEXT` and `gl_PrimitiveID`;
  miss clears the hit flag. Shadow rays use `TerminateOnFirstHit | SkipClosestHitShader`.
- One `RDPipelineShader` serves as raygen, miss and closest hit; one hit group; recursion depth 1.
  A hit SBT with a single range is assigned to every TLAS instance.
- BLAS per mesh from a triangle soup (`SceneSnapshot::blas_vertices`) in the same order as the
  software triangle buffer, so `instance.triangle_offset + gl_PrimitiveID` indexes the shared
  triangle/material data directly. Vertex buffers need
  `BUFFER_CREATION_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT | DEVICE_ADDRESS_BIT`.
- The TLAS is rebuilt when instances change (with spare capacity); instance custom index = our
  instance index, flags `TRIANGLE_FACING_CULL_DISABLE | FORCE_OPAQUE`. An empty scene gets one
  mask-0 instance so the TLAS stays valid.
- Uniform sets are identical to the software path plus the TLAS at set 1 binding 10.
- `PathTracingCamera.backend` = Auto / Software / Hardware; `get_active_backend()` reports what is
  actually running. Auto prefers hardware. Hardware requested but unsupported warns once and
  falls back to software.

### Software traversal and the NVIDIA driver

With separate 32-entry stacks for the TLAS and BLAS loops, the software shader sometimes lost the
Vulkan device (`VkResult -4`) once the temporal code was added. It always happened on the same
random path, at a frame that moved when unrelated code changed, even with every buffer index
clamped and every loop bounded. That points at the driver's handling of the shader (local
memory), not at an out-of-bounds access. Sharing one 32-entry stack between the TLAS and BLAS
walks (BLAS entries are pushed above the pending TLAS entries) fixed it and uses half the local
memory. If it ever comes back, smaller stacks or moving the temporal resolve into its own
dispatch are the next things to try.

## Temporal accumulation

The image is a per-pixel running mean (`history.a` = sample count) that is reprojected instead of
reset when the camera or objects move:

- The G-buffer stores the primary hit's normal and its distance from the camera (−1 for sky).
- The primary hit is mapped back to last frame's world position through the instance's previous
  transform (`GpuInstance::prev_object_to_world`; `GeometryGroup3D` uploads once more after motion
  stops so it catches up), then projected with last frame's view-projection. Sky pixels reproject
  by direction only.
- Bilinear taps are accepted when last frame's distance matches within 3% + 2 cm and the normals
  agree (dot > 0.8); sky only matches sky. Rejected history restarts at one sample.
- While anything moves, the history length is capped at `temporal_max_history` (16) so lighting
  changes fade in; two frames after everything stops it accumulates without limit again (or up to
  `max_samples`).
- `temporal_reprojection = false` restores plain accumulation (reset on any change). Settings,
  backend and resolution changes always reset.

Not done: variance (neighbourhood) clamping against lag in indirect light, and a spatial filter.
Light changes are only detected as "something changed" through the light buffer comparison.

## Portals

`Portal3D` (a `Node3D` under the `GeometryGroup3D`) is a rectangle of `size` in the local XY plane
whose front faces +Z, with a `linked_portal` and a `tint`.

- Rays that enter a portal's front closer than the nearest surface continue from the linked
  portal. The teleport transform is `other.global * rotY(180°) * self.global⁻¹`, so walking into
  the front of A exits the front of B. Up to `max_portal_hops` (8) crossings per segment; unlinked
  portals and the hop limit end the path (black). Each crossing multiplies throughput by `tint`.
- Portals are tested analytically in the integrator (a loop over all portals), not through the
  BVH, so a handful of portals costs almost nothing and needs no geometry.
- Next event estimation does not look through portals: a portal front blocks shadow rays, and
  emission found through a portal by BSDF sampling gets full MIS weight. Light from the other side
  therefore arrives only via BSDF-sampled paths (noisier, but unbiased).
- Temporal reprojection uses the unfolded (virtual) hit point for pixels seen through a portal,
  which is exact while the portals don't move.
- `is_crossing(from, to)` and `get_teleport_transform()` are exposed for gameplay; the demo fly
  camera uses them to walk through.
- Place a portal about a centimetre in front of the wall it sits on; when coplanar, the wall and
  the portal fight.

## Demo / testing

- `demo/demo.tscn`: the Cornell box, with both Suzannes animated (`demo/animator.gd`).
- `demo/lights_demo.tscn`: textured ground, sun + omni + spot, metal sphere, emissive lamp.
- `demo/portal_demo.tscn`: two disconnected rooms linked by a portal pair; fly through it.
- Keys: WASD/QE move, wheel = speed, Esc = mouse capture, Space = pause animation, Tab = debug
  view, B = cycle backend, T = toggle temporal reprojection.
- `demo/capture.gd` is a dev helper for scripted regression screenshots (opens a window):

```bash
Godot_v4.7.2-stable_win64_console.exe --path project --resolution 1280x720 -- --capture=out.png --capture-frames=300 --static
```

  Extra flags: `--raster` (disable the path tracer for comparison), `--debug-view=N`, `--static`
  (freeze animations so accumulation converges), `--backend=0|1|2` (auto/software/hardware),
  `--no-temporal`, `--pan` (sway the camera), `--walk` (portal demo: walk through portal A). It
  prints the average frame time and the viewport's GPU time.

## Known limitations / follow-ups

- Noise: 1 spp + white-noise PCG. Blue noise / Sobol and a spatial denoiser are the obvious next
  steps; temporal reuse alone still looks grainy while things move.
- Many emissive triangles: NEE picks one triangle per bounce. Fine for the demos; ReSTIR DI or a
  light BVH would be needed for scenes with thousands of emitters.
- Textures are resized into one `texture_resolution`² array without mipmaps (no ray cones), so
  distant textures alias. Normal/roughness/metallic/emission textures are ignored. No alpha
  testing or transmission.
- Möller–Trumbore is not watertight: an occasional pixel leaks through shared triangle edges.
- TLAS rebuild is O(n²) agglomerative clustering on the CPU. Fine for hundreds of instances; for
  thousands, switch to binned SAH or refit.
- The sky is a simple gradient set on the camera, not the `WorldEnvironment` sky.

- Hardware path: BLASes are rebuilt from scratch on every geometry change and the TLAS on every
  instance change (no refit/update). If Godot/the driver ever report `SUPPORTS_RAY_QUERY`, ray
  queries would allow one compute shader for both backends.

## Next steps

1. **Spatial denoising.** SVGF-style à-trous on top of the temporal history, with albedo
   demodulation and variance from history moments. The G-buffer already has normal + distance;
   add albedo.
2. **Variance clamping** in the temporal pass to reduce lag in indirect light.
3. **Portal puzzle demo.** A player controller with collision that teleports via
   `Portal3D.is_crossing()`, plus moving portals (needs previous-frame portal transforms for
   reprojection).
4. **NEE through portals** (for example, sampling lights in each portal's linked space) to cut the
   noise of light that only reaches a room through a portal.
