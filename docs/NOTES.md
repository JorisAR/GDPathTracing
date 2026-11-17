# Renderer modernization notes

Notes on the rework that brought the plugin to Godot 4.7 and implemented roadmap phases 0–3.
Phases 4+ (hardware ray tracing, portals, denoising, demo game) are still to do; see the end.

## TL;DR

| | Before | After |
|---|---|---|
| Godot / bindings | 4.3, godot-cpp `4.3` + GDCS submodule | 4.7, godot-cpp `10.0.0-stable` (`api_version=4.7`), no GDCS |
| Presentation | Local RenderingDevice → `texture_get_data` (GPU→CPU) → `Image` → `ImageTexture` (CPU→GPU) → `TextureRect`, every frame | `CompositorEffect` writing straight into the scene's HDR color buffer on the main RenderingDevice |
| Color precision | `rgba8` radiance, clipped at 1.0 before accumulation | `rgba16f` output, `rgba32f` accumulation, Godot's tonemapper/exposure/glow apply |
| Workgroups | 32×32 (1024 threads, poor occupancy) | 8×8 |
| Lighting | BSDF sampling only | NEE on emissive triangles (power-proportional CDF) + MIS, plus directional/omni/spot lights with soft shadows |
| Scene | Built once, static | Instance transforms/visibility/lights tracked every frame, TLAS rebuilt on change |

Measured on an RTX 4070 Ti SUPER (whole frame, including Godot's own raster pass and UI):

- Cornell demo, 1080p, 5 bounces, animated (1 spp/frame): **~9.5 ms**
- Cornell demo, 720p, animated: **~4.4 ms**
- Lights demo (5k tris, 3 punctual lights + emissive), 720p, static: **~2.4–5 ms**

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
2. Gets an `rgba32f` accumulation texture from `RenderSceneBuffersRD` (context
   `jar_path_tracing`). It follows viewport resizes automatically.
3. Builds the camera ray setup from `RenderSceneData::get_cam_transform()` and
   `get_cam_projection()`. That projection is reversed-Z and already contains Vulkan's y flip, so
   NDC y increases downwards in the shader.
4. Dispatches `main.glsl` once (8×8 groups), which traces, accumulates (running mean) and writes
   linear HDR radiance into the color buffer. Godot's tonemapping etc. happen afterwards, so the
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
  main.glsl        entry point: camera rays, integrator, NEE, accumulation
  common.glsli     structs/bindings (mirror of gpu_types.h + FrameData), RNG, helpers
  brdfs.glsli      Burley-ish diffuse + GGX (VNDF sampling), ShadingInfo
  trace_sw.glsli   software two-level BVH traversal: trace_closest() / trace_any()
```

Includes use `.glsli` so Godot does not try to import them as standalone shaders. The hardware
backend (phase 4) only needs to provide another `trace_*.glsli` with the same two functions.

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

## Demo / testing

- `demo/demo.tscn`: the Cornell box, with both Suzannes animated (`demo/animator.gd`).
- `demo/lights_demo.tscn`: textured ground, sun + omni + spot, metal sphere, emissive lamp.
- Keys: WASD/QE move, wheel = speed, Esc = mouse capture, Space = pause animation, Tab = debug view.
- `demo/capture.gd` is a dev helper for scripted regression screenshots (opens a window):

```bash
Godot_v4.7.2-stable_win64_console.exe --path project --resolution 1280x720 -- --capture=out.png --capture-frames=300 --static
```

  Extra flags: `--raster` (disable the path tracer for comparison), `--debug-view=N`, `--static`
  (freeze animations so accumulation converges). It prints the average frame time.

## Known limitations / follow-ups

- Noise: 1 spp + white-noise PCG. Blue noise / Sobol and a denoiser (phase 6) are the obvious
  next steps. Moving anything resets accumulation; there is no temporal reuse yet.
- Many emissive triangles: NEE picks one triangle per bounce. Fine for the demos; ReSTIR DI or a
  light BVH would be needed for scenes with thousands of emitters.
- Textures are resized into one `texture_resolution`² array without mipmaps (no ray cones), so
  distant textures alias. Normal/roughness/metallic/emission textures are ignored. No alpha
  testing or transmission.
- Möller–Trumbore is not watertight: an occasional pixel leaks through shared triangle edges.
- TLAS rebuild is O(n²) agglomerative clustering on the CPU. Fine for hundreds of instances; for
  thousands, switch to binned SAH or refit.
- The sky is a simple gradient set on the camera, not the `WorldEnvironment` sky.

## Next phases (not started)

4. **Hardware backend.** Godot 4.7 exposes `blas_create`/`tlas_create`,
   `raytracing_pipeline_create`, hit SBTs and `raytracing_list_trace_rays`, behind
   `RenderingDevice.has_feature(SUPPORTS_RAYTRACING_PIPELINE)`. Plan: raygen shader runs the
   same integrator, closest-hit only returns (instance, primitive, barycentrics, t), miss returns
   no-hit, shadow rays use `gl_RayFlagsTerminateOnFirstHitEXT`. BLAS per `MeshEntry`, TLAS
   instances from the same `GpuInstance` data. Keep the compute path as the fallback.
5. **Portals.** In the integrator: when a hit's material/instance is flagged as a portal, apply
   `dst * inverse(src)` to the ray and continue without counting a bounce (cap at ~8 hops). NEE
   through portals is the open question.
6. **Denoising.** Temporal accumulation with motion vectors and history rejection, then SVGF-style
   à-trous with albedo demodulation. Portal hits need to count as disocclusions.
7. **Portal puzzle demo.**
