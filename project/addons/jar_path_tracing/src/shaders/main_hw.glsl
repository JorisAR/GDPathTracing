#[raygen]
#version 460

// Hardware backend: the same integrator runs in a ray generation shader and traces with
// traceRayEXT against a Godot TLAS/BLAS (RT cores). Closest-hit only reports what was hit;
// all shading happens here, exactly like the software path.

#extension GL_EXT_ray_tracing : require

#define PIXEL_ID gl_LaunchIDEXT.xy

#include "common.glsli"
#include "brdfs.glsli"
#include "hw_payload.glsli"
#include "trace_hw.glsli"
#include "integrator.glsli"

void main() {
    pt_main();
}

#[miss]
#version 460
#extension GL_EXT_ray_tracing : require

#include "hw_payload.glsli"
layout(location = 0) rayPayloadInEXT HwPayload payload;

void main() {
    payload.hit = 0u;
}

#[closest_hit]
#version 460
#extension GL_EXT_ray_tracing : require

#include "hw_payload.glsli"
layout(location = 0) rayPayloadInEXT HwPayload payload;
hitAttributeEXT vec2 attribs;

void main() {
    payload.t = gl_HitTEXT;
    payload.bary = attribs;
    payload.instance = uint(gl_InstanceCustomIndexEXT);
    payload.primitive = uint(gl_PrimitiveID);
    payload.hit = 1u;
}
