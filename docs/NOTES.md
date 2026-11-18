# Renderer modernization notes

Notes on the rework that brought the plugin to Godot 4.7. Phases 0–3 (toolchain, pipeline,
lighting, dynamic scenes) came first; then the hardware ray tracing backend, temporal
accumulation and portals; then a spatial denoiser, a bigger portal demo and an editor preview. The
portal puzzle game is still to do (see the end).

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
| Denoising | — | SVGF-style à-trous filter with albedo demodulation and temporal variance |
| Indirect light | One path per pixel | ReSTIR GI (temporal + spatial reuse), compatibility-guided neighbour selection |
| Editor | Nothing in the editor | `editor_preview` path traces the editor's 3D viewport |

Measured on an RTX 4070 Ti SUPER, GPU time of the whole viewport (including Godot's own raster
pass and UI), from `capture.gd`:

| Scene (1080p, animated, temporal on) | Software (compute BVH) | Hardware (RT pipeline) |
|---|---|---|
| Cornell demo, 5 bounces, denoiser on | 10.5 ms | 4.9 ms |
| Cornell demo, denoiser off | 8.9 ms | 3.3 ms |
| Portal demo (6 portals, recursive views), denoiser on | 12.5 ms | 8.3 ms |
| Portal demo, denoiser off | 11.0 ms | 6.6 ms |

The denoiser (4 iterations) costs about 1.5 ms at 1080p. Both backends converge to the same image
(mean difference < 0.05/255 on the Cornell and lights demos).

I did not benchmark the old build (it needed the old GDCS + godot-cpp 4.3 toolchain), so there is
no exact before/after number. The old per-frame GPU→CPU→GPU round trip alone typically costs
several milliseconds at 1080p and forces a GPU sync.

## Repository layout and CI

The Godot project lives in `demo/` (it used to be `project/`): `demo/addons/jar_path_tracing` is
the addon, `demo/demo/` the demo scenes. `.github/workflows/build.yml` builds Linux and Windows
(debug and release; Windows via MinGW on Ubuntu) on every push and pull request to `main`. Each
job uploads a zip of `demo/` without `.godot/` and `bistro/`. On a GitHub release, the zips are
attached to it. The Android builds were dropped.

## Phase 0: toolchain

- `godot-cpp` submodule moved to tag `10.0.0-stable`. godot-cpp 10 ships API files for 4.3–4.7;
  `SConstruct` defaults `api_version=4.7` (needed for the 4.7-only RenderingDevice ray tracing API
  we will use in phase 4). The stale root `extension_api.json` was removed.
- GDCS removed. Its job is now `src/rd/rd_util.*`: a growable `GpuBuffer` and a `make_uniform`
  helper. Uniform sets go through Godot's `UniformSetCacheRD`, so there is nothing to free by hand.
- `demo/project.godot` is now tracked. It was previously ignored by `*.godot` in the root
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
2. Gets its textures (ping-pong history, G-buffer and moments; per-frame color, motion and albedo;
   denoiser scratch) from `RenderSceneBuffersRD` (context `jar_path_tracing`). They follow
   viewport resizes automatically.
3. Builds the camera ray setup from `RenderSceneData::get_cam_transform()` and
   `get_cam_projection()`. That projection is reversed-Z and already contains Vulkan's y flip, so
   NDC y increases downwards in the shader.
4. Runs these passes:
   - **trace:** `main.glsl` (compute, 8×8 groups) or `main_hw.glsl` (ray tracing pipeline) traces
     one path per pixel and writes this frame's color, primary albedo, G-buffer and motion.
   - **ReSTIR GI** (optional, on by default): `restir.glsl` / `restir_hw.glsl` run twice, temporal
     then spatial reuse, and add the resampled indirect light to this frame's color.
   - **resolve:** `resolve.glsl` reprojects and accumulates the history, then writes linear HDR
     radiance into the color buffer.
   - **denoise** (optional): `denoise.glsl` filters the accumulated image and overwrites the color
     buffer.

   Godot's tonemapping etc. happen afterwards, so the `WorldEnvironment` tonemap settings control
   the look.

Accumulation resets on camera, projection, settings, scene or resolution changes, or when
`reset_accumulation()` is called. With `max_samples > 0` it stops tracing and just copies the
accumulated image each frame.

Camera properties: `max_bounces`, `samples_per_pixel`, `accumulate`, `max_samples`,
`firefly_clamp`, `debug_view` (None / Normals / Albedo / Traversal cost), sky gradient colors +
`sky_energy`, `path_tracing_enabled`, `backend`, `temporal_reprojection`, `temporal_max_history`,
`max_portal_hops`, `restir_gi`, `restir_neighbors`, `restir_candidates`, `restir_radius`,
`restir_guided_selection`, `restir_di`, `restir_di_candidates`, `environment_map`,
`environment_energy`, `environment_rotation`, `denoise`, `denoise_iterations`, `editor_preview`.

Things to know:

- Godot still rasterizes the scene underneath, then we overwrite it (cost not measured separately;
  small for these demos). For heavy scenes, put the meshes on a render layer the camera's
  `cull_mask` excludes (the path tracer does not look at layers).
- Disable TAA/FSR on the viewport: they would reproject our image with the raster's motion vectors.
- Only Forward+ (needs RenderingDevice and an `rgba16f` color buffer). XR multiview is not handled
  (view 0 only). Orthographic cameras are not handled.
- In the editor, nothing runs unless a camera's `editor_preview` is on (see below).

### Shader layout

```
shaders/
  main.glsl        software entry point (compute): includes common, brdfs, trace_sw, integrator
  main_hw.glsl     hardware entry point: #[raygen] (same includes with trace_hw), #[miss], #[closest_hit]
  integrator.glsli camera rays, path tracing loop, NEE + MIS, portals, motion vectors
  resolve.glsl     temporal reprojection + accumulation (history, albedo, luminance moments)
  denoise.glsl     spatial denoiser (variance estimate + a-trous iterations)
  restir.glsl      ReSTIR GI pass, software entry (compute, visibility via trace_sw)
  restir_hw.glsl   ReSTIR GI pass, hardware entry (#[raygen] + #[miss] + #[closest_hit])
  restir.glsli     ReSTIR GI: reservoirs, temporal reuse, neighbour selection, spatial reuse
  restir_di.glsli  ReSTIR DI: light candidates, temporal and spatial reuse of light samples
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
- Material textures: colors (albedo, emission) go to an sRGB texture array, roughness, metallic
  and normal maps to a second, linear one (`data_textures`). Roughness and metallic follow Godot:
  value × texture[channel]. Normal maps use a per-triangle tangent from the UVs, orthogonalized
  against the interpolated normal in the shader; the bitangent follows +v (DirectX), so OpenGL
  style maps (Godot's default) flip green unless `GeometryGroup3D.directx_normal_maps` is on. z is
  rebuilt from xy. Compressed textures with mipmaps (DDS) only decompress the smallest mip that is
  at least `texture_resolution` wide.
- Alpha (albedo color alpha times albedo texture alpha) is alpha tested for every transparency
  mode: scissor uses its threshold; blend, hash and depth prepass use stochastic transparency (keep
  the hit with probability alpha, hashed from triangle, barycentrics and frame index, so closest
  and shadow rays agree within a frame and accumulation converges to the blend). Godot's FBX
  importer gives Bistro's foliage depth prepass, which was previously treated as opaque. Only
  materials whose alpha can actually reject a hit are alpha tested: in the software traversal per triangle (flag in `Triangle.v0.w`), and on hardware
  through an any-hit shader on non-opaque BLASes (meshes without cutouts stay opaque, so they never
  run it). Rays no longer use the opaque ray flag.
- Environment map: `PathTracingCamera.environment_map` (an equirectangular HDR) with
  `environment_energy` and `environment_rotation` replaces the gradient sky. It is box filtered to
  at most 2048 wide and importance sampled per bounce (luminance × sin θ, marginal + conditional
  CDFs, `environment.cpp`), MIS-weighted against BSDF hits. Checked against BSDF-only sampling on a
  smooth test environment: mean 211.83 vs 211.81 after 2000 frames.
- Emission textures: `GpuMaterial.emission_texture` and `emission_energy` follow Godot's
  operators (color + texture or color × texture, times energy). The light sampling CDF uses each
  triangle's emission averaged over its corners and centroid. Every triangle of an emissive
  instance gets a CDF entry (zero power if it doesn't emit), and `GpuInstance.emissive_offset`
  points at the instance's first one. So when a BSDF ray hits an emitter, the MIS weight uses
  exactly the probability the light sampler would have used, instead of the hit point's luminance.
- Shading normals: the geometric normal is first oriented to agree with the interpolated vertex
  normal, because Godot's clockwise winding makes `cross(e1, e2)` point inwards. Before this fix,
  the back-face flip turned the shading normal away from the viewer on almost every triangle and
  the silhouette fallback replaced it with the flat geometric normal, so smooth meshes rendered
  faceted (dense ones like `SphereMesh` hid it).
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

**Keep the raygen state small.** In a ray tracing pipeline, everything the raygen shader still
needs after a `traceRayEXT` call is spilled to the ray tracing stack at every trace. With the whole
integrator in raygen, that is every bounce's shadow and continuation rays. When ReSTIR GI added a
`GiSample` and the per-pixel `Primary` struct stayed live through the path loop, the hardware
backend became slower than the software one (Cornell at 1440p: 32 ms vs 27 ms). `path_trace()`
now writes motion, G-buffer, albedo and the ReSTIR inputs to their images the moment they are
known, so only the radiance, throughputs and two small GI values stay live: 12.7 ms hardware vs
21.0 ms software, and 0.62 ms vs 2.45 ms at 360p. Same images, bit for bit.

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

### Software tracing kernel and the NVIDIA driver

The software kernel lost the Vulkan device (`VkResult -4`; the Windows event log shows nvlddmkm
event 153, a GPU exception, not a 4101 timeout) whenever it grew past some size:

- The first time was when the temporal code went into the kernel. It always happened on the same
  random path, at a frame that moved when unrelated code changed, even with every buffer index
  clamped and every loop bounded.
- Sharing one 32-entry stack between the TLAS and BLAS walks (BLAS entries pushed above the
  pending TLAS entries) hid it, until the denoiser outputs were added to the same kernel.

The lasting fix was structural. The tracing kernel no longer reads any history: it only writes
this frame's results, and accumulation lives in the small `resolve.glsl` pass. Since then the
software path has run over 10,000 frames across all demos without a fault (and got slightly
faster). Keep the tracing kernel lean; put new per-pixel work in a separate pass.

## Temporal accumulation

The image is a per-pixel running mean (`history.a` = sample count) that is reprojected instead of
reset when the camera or objects move. The tracing pass writes the inputs and `resolve.glsl` does
the rest:

- The G-buffer stores the primary hit's normal and its distance from the camera (−1 for sky).
- The primary hit is mapped back to last frame's world position through the instance's previous
  transform (`GpuInstance::prev_object_to_world`; `GeometryGroup3D` uploads once more after motion
  stops so it catches up), then projected with last frame's view-projection. Sky pixels reproject
  by direction only.
- That screen motion is applied to the pixel centre, not to the jittered sample position.
  Otherwise a static view resamples the history bilinearly by a sub-pixel offset every frame and
  the converged image slowly blurs.
- Bilinear taps are accepted when last frame's distance matches within 3% + 2 cm and the normals
  agree (dot > 0.8); sky only matches sky. Rejected history restarts at one sample.
- While nothing has moved for two frames (`FLAG_STATIC`), the history is used at the same pixel
  without those tests. Camera rays are jittered, so an edge pixel sees a different surface from
  frame to frame; rejecting that restarted edge pixels every frame and left single-sample speckles
  along every edge of a still image. While moving, the tests still apply, and edges are speckled
  until the view settles (a better disocclusion test is a follow-up).
- While anything moves, the history length is capped at `temporal_max_history` (16) so lighting
  changes fade in; two frames after everything stops it accumulates without limit again (or up to
  `max_samples`).
- `temporal_reprojection = false` restores plain accumulation (reset on any change). Settings,
  backend and resolution changes always reset.

Alongside the color, the resolve accumulates the primary albedo and the mean of luminance² with
the same weights, for the denoiser.

Not done: variance (neighbourhood) clamping against lag in indirect light.

## Spatial denoiser

`denoise.glsl`, SVGF without its temporal half (the resolve pass already does that):

1. **Prepare:** divide the accumulated color by the accumulated albedo (demodulation, so texture
   detail is never blurred) and estimate the variance of the accumulated mean. That is
   (E[l²] − E[l]²) / n from the moments; while a pixel has fewer than 4 samples, a 3×3 spatial
   estimate is used instead.
2. **Filter:** `denoise_iterations` (default 4) à-trous iterations of a 5×5 B3-spline kernel with
   steps 1, 2, 4, 8 pixels. Each weight is stopped by:
   - geometry: the pixel compatibility heuristic of Junkins et al. 2026 (*Compatibility-Guided
     Neighbor Selection for ReSTIR*, eqs. 14/15), `exp(-|x−y|/s) · max(n·n',0)^β` with the primary
     hits reconstructed in world space from the G-buffer distance and `s = sqrt(Ω/π)·d`
     (Ω = 0.05 as in the paper, β = 64 instead of the paper's 8 because a filter averages radiance
     directly);
   - plane distance: `|n·(y−x)|` against the pixel footprint times the tap offset, which separates
     surfaces at different depths and replaces the old screen-space depth gradient (slanted floors
     still filter);
   - illumination luminance: relative to sqrt of the 3×3-blurred variance.

   While a pixel has fewer than 4 samples, the variance comes from K = 32 candidates in an 8-pixel
   disk (R2 sequence + concentric mapping, randomized start, as in the paper's Section 5), weighted
   by the heuristic instead of a binary-rejected 3×3. On the Cornell and lights demos this scores
   the same as the old filter (SMAPE vs. a 3000-sample reference within 0.01 at 4 and 16 samples);
   the heuristic is mainly groundwork for a ReSTIR spatial pass, which is where the paper applies it.

   Variance is propagated with the squared weights. The last iteration multiplies the albedo back
   in and writes the color buffer.

Because the luminance stop scales with the variance of the *accumulated* mean, the filter fades
out as samples accumulate. A static view still converges to the unfiltered result: after 1,500
frames the Cornell box differs by 0.3/255 from the undenoised image. The sky is not filtered.
Debug views skip the denoiser. `denoise = false` (N in the demos) turns it off.

Costs about 1.5 ms at 1080p with 4 iterations on the test GPU. Known weaknesses: some low-frequency
blotching on large diffuse walls in the first frames after a change, and a softened look while
the history cap (16) is active during motion.

## ReSTIR GI

Indirect light at the primary hit is resampled across pixels and frames (Ouyang et al. 2021), with
spatial neighbours picked by the compatibility-guided selection of Junkins et al. 2026,
*Compatibility-Guided Neighbor Selection for ReSTIR*. `restir_gi = false` (R in the demos)
restores plain path tracing.

**Trace pass split.** With `FrameData.flags & FLAG_RESTIR`, the integrator splits each path at the
primary hit x1:

- direct light at x1 (its emission, NEE, and the MIS-weighted emission found by the BSDF ray at
  x2) stays in the frame color, and so do paths whose second segment hits the sky, crosses a
  portal or is absorbed;
- the radiance leaving x2 towards x1, excluding x2's own emission, becomes the GI sample: x2, its
  geometric normal, that radiance and the solid angle pdf of x1 → x2 (`gi_sample`,
  `gi_radiance`). The primary hit is stored as instance/triangle/barycentrics (`gi_hit`), so the
  ReSTIR pass rebuilds x1's shading exactly (`get_shading`).

Pixels whose primary hit is seen through a portal don't take part (plain paths), because the
reconnection x1 → x2 is a straight segment. With `samples_per_pixel > 1` only the first path
feeds ReSTIR.

**Pass 1, temporal.** The pixel's sample becomes a reservoir (W = 1/pdf, M = 1). It is merged with
last frame's *final* reservoir at the reprojected pixel, accepted with the resolve pass's surface
test, with M capped at 20 (the paper's cap). The target function is the luminance of
brdf(x1) · cos · L(x2→x1), re-evaluated at the current x1, with 1/M weights (standard ReSTIR GI).

**Pass 2, spatial.**

1. *Neighbour selection* (the paper's Algorithm 1): K = `restir_candidates` (32) pixels from an
   R = `restir_radius` (30 px) disk, via an R2 sequence with a randomized start and the concentric
   mapping. Each is scored with h = exp(−|x−y|/s) · max(n·n', 0)^8, where s = sqrt(Ω/π)·d and
   Ω = 0.05, from the G-buffer. M = `restir_neighbors` (default 1, max 2) are picked with weighted
   reservoir sampling (A-ES), stopping early once more than M candidates score h > 0.5.
   `restir_guided_selection = false` switches to the baseline: M uniform disk samples kept if they
   pass the binary test of the paper's eq. 7 (n·n' ≥ 0.5, |Δd|/d < 0.1).
2. *Resampling* over the pixel and its neighbours with the generalized balance heuristic,
   m_j(y) = M_j p_j(y) / Σ_l M_l p_l(y). Every p is evaluated in area measure at x2, so the
   reconnection Jacobians cancel, and it includes visibility (rays from each domain's x1 to the
   candidate's x2). Leaving visibility out of the denominators darkened the Cornell box by about
   4%.
3. The chosen sample is shaded at x1 and added to the frame color. The reservoir (M = sum of
   confidences) becomes next frame's temporal input.

Results at 640×360 (Cornell box, static, hardware backend, single frames with no accumulation,
SMAPE against a 1000-sample path traced reference):

| | SMAPE | RMSE (8-bit) |
|---|---|---|
| Path tracing | 18.0 | 55.6 |
| ReSTIR, temporal only | 8.5 | 32.2 |
| ReSTIR, temporal + spatial (1 neighbour) | 8.0 | 31.8 |
| ReSTIR, temporal + spatial (2 neighbours) | 7.6 | 30.6 |

On the lights demo the gain is smaller (4.8 → 4.5); its light is mostly direct sun, which ReSTIR
GI does not touch. Accumulated over 300 frames, ReSTIR converges to the reference (mean luminance
150.5 vs 151.1; path tracing 150.8). Both backends give the same result.

Guided vs. uniform neighbour selection tie on the small demos (7.97 vs 7.86 on the Cornell box,
4.53 vs 4.50 on the lights demo, within frame-to-frame noise). That fits the paper: its gains are
around geometric detail (foliage, thin parts, grazing angles), where few uniform neighbours pass
the binary test.

The Bistro interior (`bistro_demo.tscn`, lit only by its emissive lamps, lots of chairs, bottles
and glasses) is where the reuse matters. Single frames at 640×360 against a 1000-sample reference:

| Bistro interior | SMAPE |
|---|---|
| Path tracing | 68.9 |
| ReSTIR, temporal only | 54.8 |
| + uniform neighbours (Bitterli), 1 / 2 | 50.9 / 50.2 |
| + guided neighbours (paper), 1 / 2 | 50.3 / 49.8 |

Guided selection wins, but only by about 1%, not the paper's 6–29%. Most of the remaining noise
there was direct light, which ReSTIR GI leaves alone; that is what ReSTIR DI (below) is for.

The Bistro exterior (the current `bistro_demo.tscn`: HDR environment, sun, 20,638 emissive
triangles, foliage) with both ReSTIR GI and DI, single frames at 640×360 against a 1000-sample
path traced reference:

| Bistro exterior | SMAPE | RMSE (8-bit) |
|---|---|---|
| Path tracing | 67.0 | 114.6 |
| ReSTIR GI | 39.9 | 82.1 |
| ReSTIR DI | 29.6 | 55.8 |
| ReSTIR GI + DI, uniform neighbours (Bitterli) | 20.8 | 46.3 |
| ReSTIR GI + DI, guided neighbours (paper) | 18.2 | 41.2 |

Here the paper's selection gives 12% lower SMAPE than the uniform baseline, inside its reported
6–29% range. With accumulation and the denoiser on, the difference mostly shows while moving.

Cost on an RTX 3080 at 1440p (Cornell, no denoiser, hardware backend): path tracing 12.7 ms,
with ReSTIR GI 11.7 ms (the difference is noise; the pass itself is cheap on RT cores). On the
software backend: 21.0 ms vs 29.5 ms, since its visibility rays walk the compute BVH. Bistro
exterior at 1080p with the denoiser: path tracing 27.8 ms, + GI 34.3 ms, + DI 36.0 ms, both
42.2 ms. With 4 neighbours it was 89 ms at 1080p (Cornell): the per-domain arrays spilled
registers and the visibility-aware MIS needs O(n²) rays, so the maximum is 2.

## ReSTIR DI

`restir_di.glsli` (on by default, `restir_di = false` turns it off) resamples direct light from
emissive triangles and the environment map at directly visible primary hits (Bitterli et al.
2020). Punctual lights keep plain NEE: there are few of them and each is sampled every bounce.

- **Samples** are points on emitters (target and densities in area measure) or environment
  directions (solid angle). Reuse at another pixel keeps the point or direction fixed, so no shift
  Jacobian is needed.
- **Trace pass:** at a reusable x1 it skips emitter and environment NEE. If the BSDF ray from x1
  hits an emitter or the sky, that becomes a DI candidate (`di_hit_a/b`: point or direction,
  emitted radiance, normal, and the light sampler's pdf for it) instead of an MIS-weighted
  contribution. Emitters reached through a portal keep full weight, as before.
- **Temporal pass:** `restir_di_candidates` (8) light samples (environment or emitter, 50/50 when
  both exist) plus the BSDF candidate, resampled with balance-heuristic weights
  p-hat / (M_light p_light + p_bsdf). The chosen sample gets a shadow ray (visibility reuse), then
  it is merged with last frame's final reservoir (1/M weights, M capped at 20).
- **Spatial pass:** the same guided neighbour selection and visibility-aware generalized balance
  heuristic as GI, then a shadow ray for the shaded sample and the result is added to the frame
  color. The reservoir becomes next frame's temporal input.

On the Cornell box (one big ceiling light, which plain NEE already handles well) DI barely helps
(single-frame SMAPE 18.0 → 17.1); on the Bistro exterior it is the bigger win of the two (see
above).

**Known bias:** accumulated over 1000 frames, the Bistro exterior with ReSTIR is about 1% darker
than the path traced reference (132.4 vs 133.7 mean, 8-bit), mostly from DI; GI alone is within
0.2%. It does not shrink with more frames, and it is not the firefly clamp (same result with the
clamp off). The likely cause is the 1/M weights in the temporal passes: with camera jitter, the
previous frame's x1 is not exactly the current one, so the two domains' targets differ slightly. A
generalized balance heuristic with the previous frame's surface (as in the spatial pass) would
remove it, at the cost of storing that surface.

Implementation gotchas:

- **No push constants in Godot ray tracing pipelines (4.7.2):** the pipeline layout leaves the
  raygen stage out of the push constant range, so a raygen shader that declares push constants
  gives an invalid pipeline. The dispatch is then silently dropped (only `--gpu-validation`
  reports it). `restir.glsli` takes its parameters from a small uniform buffer per pass instead.
- **Don't bit-cast integers into float images:** `uintBitsToFloat` of small integers gives denormals,
  which GPUs may flush to zero on store. Indices and packed normals are stored as integer-valued
  floats (exact below 2^24).

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
- `demo/bistro_demo.tscn`: the Amazon Lumberyard Bistro exterior (2.83M triangles, 1,296
  instances) under the `san_giuseppe_bridge_4k.hdr` environment (energy 10, as in Falcor's scene
  file) and the FBX's sun at energy 3. The 1.7 GB download is not in the repository; see
  `demo/bistro/README.md`. `demo/bistro_setup.gd` links each material's `_Specular` texture as
  roughness (green) and metallic (blue), turns on alpha scissor for base colors that can carry
  alpha (DXT5/BC7; only real cutouts are alpha tested), and handles the FBX lights and emission
  scale. The group sets `directx_normal_maps`. Building the scene takes about 10 s (405 textures).
  Glass is opaque (no transmission).
- `demo/portal_demo.tscn`: a hall with three portal pairs:
  - an endless corridor between the side walls (violet frames, slightly tinted so each recursion
    is darker);
  - a fall loop between a floor hole and the ceiling above it (gold frames);
  - a door on the back wall that opens onto a sunlit garden far away, where it comes out of a
    free-standing frame.

  Fly through any of them. The scene is generated by `tools/gen_portal_demo.py` (edit the script,
  not the `.tscn`).
- Keys: WASD/QE move, wheel = speed, Esc = mouse capture, Space = pause animation, Tab = debug
  view, B = cycle backend, T = toggle temporal reprojection, R = toggle ReSTIR GI, N = toggle the
  denoiser.
- `demo/capture.gd` is a dev helper for scripted regression screenshots (opens a window):

```bash
Godot_v4.7.2-stable_win64_console.exe --path demo --resolution 1280x720 -- --capture=out.png --capture-frames=300 --static
```

  Extra flags: `--raster` (disable the path tracer for comparison), `--debug-view=N`, `--static`
  (freeze animations so accumulation converges), `--backend=0|1|2` (auto/software/hardware),
  `--no-temporal`, `--no-denoise`, `--no-restir`, `--no-accumulate` (show single frames; ReSTIR
  temporal reuse keeps running), `--set=property=value` (any camera property, parsed with
  `str_to_var`), `--env=res://map.hdr[,energy]` (environment map), `--pan` (sway the camera), `--walk` (portal demo: walk forward
  until through the garden door), `--camera=x,y,z,yaw,pitch` (degrees). It ignores the real mouse
  and prints the average frame time and the viewport's GPU time.

## Editor preview

Tick `editor_preview` on a `PathTracingCamera` to path trace the editor's main 3D viewport, from
the editor's own camera and with that camera's settings:

- The effect goes on a fresh `Compositor` on the editor viewport's internal camera, so nothing is
  written into the scene. Unticking it, deleting the camera or closing the scene removes it again.
- The `GeometryGroup3D` builds on first use and then tracks transforms, visibility and lights
  while the preview runs, so moving things in the editor updates live.
- Mesh, material or node add/remove changes need the **Rebuild Scene** button in the camera's
  Editor group.
- While previewing, the editor's low-processor mode is switched off so accumulation keeps going;
  the previous setting is restored afterwards.
- Only viewport 1 of a split view is path traced.
- Editor gizmos (grid, selection outlines) are painted over while the preview is on.

## Known limitations / follow-ups

- Noise: 1 spp + white-noise PCG. Blue noise / Sobol sequences would help the denoiser most.
- Many emissive triangles: NEE picks one triangle per bounce. Fine for the demos; ReSTIR DI or a
  light BVH would be needed for scenes with thousands of emitters. Direct light is now the
  dominant noise left with ReSTIR GI on, so ReSTIR DI (same reservoir and neighbour selection
  code) is the natural next step.
- ReSTIR GI: the temporal pass uses 1/M weights without a visibility check, so moving occluders
  can leave stale light for up to 20 frames' worth of confidence. Glossy primary surfaces reuse
  poorly (their target is sharp), and Lo at x2 is assumed independent of the direction to x1.
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

1. **Variance clamping** in the resolve pass to reduce lag in indirect light, and feeding the first
   à-trous iteration back into the history (as SVGF does) for more stable motion.
2. **Portal puzzle demo.** A player controller with collision that teleports via
   `Portal3D.is_crossing()`, plus moving portals (needs previous-frame portal transforms for
   reprojection).
3. **NEE through portals** (for example, sampling lights in each portal's linked space) to cut the
   noise of light that only reaches a room through a portal; the garden light in the portal demo
   is the obvious test case.
4. **Editor preview polish:** draw gizmos on top, preview through any of the split viewports, and
   rebuild automatically when the scene tree changes.
