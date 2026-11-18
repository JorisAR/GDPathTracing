#[versions]

gi_temporal = "#define RESTIR_MODE 0";
gi_spatial = "#define RESTIR_MODE 1";
di_temporal = "#define RESTIR_MODE 2";
di_spatial = "#define RESTIR_MODE 3";

#[raygen]
#version 460

#VERSION_DEFINES

// ReSTIR GI pass, hardware backend (visibility rays with traceRayEXT). See restir.glsli.

#extension GL_EXT_ray_tracing : require

#extension GL_EXT_control_flow_attributes : require

#define RESTIR_PASS

#include "common.glsli"
#include "brdfs.glsli"
#include "hw_payload.glsli"
#include "trace_hw.glsli"
#include "integrator.glsli"
#include "restir.glsli"
#include "restir_di.glsli"

void main() {
    restir_main(ivec2(gl_LaunchIDEXT.xy));
}

#[miss]
#version 460

#VERSION_DEFINES
#extension GL_EXT_ray_tracing : require

#include "hw_payload.glsli"
layout(location = 0) rayPayloadInEXT HwPayload payload;

void main() {
    payload.hit = 0u;
}

#[closest_hit]
#version 460

#VERSION_DEFINES
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

#[any_hit]
#version 460

#VERSION_DEFINES
#extension GL_EXT_ray_tracing : require

// Alpha scissor for non-opaque (alpha tested) geometry.
#extension GL_EXT_control_flow_attributes : require

#define RESTIR_PASS

#include "common.glsli"

hitAttributeEXT vec2 attribs;

void main() {
    uint instance = uint(gl_InstanceCustomIndexEXT);
    uint triangle = instances[instance].triangle_offset + uint(gl_PrimitiveID);
    if (triangles[triangle].v0.w > 0.5 && !alpha_test_pass(instance, triangle, attribs)) ignoreIntersectionEXT;
}
