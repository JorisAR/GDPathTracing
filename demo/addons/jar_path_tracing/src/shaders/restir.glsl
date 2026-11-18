#[versions]

gi_temporal = "#define RESTIR_MODE 0";
gi_spatial = "#define RESTIR_MODE 1";
di_temporal = "#define RESTIR_MODE 2";
di_spatial = "#define RESTIR_MODE 3";

#[compute]
#version 460

#VERSION_DEFINES

// ReSTIR GI pass, software backend (visibility rays through the compute BVH). See restir.glsli.

#extension GL_EXT_control_flow_attributes : require

#define RESTIR_PASS

#include "common.glsli"
#include "brdfs.glsli"
#include "trace_sw.glsli"
#include "integrator.glsli"
#include "restir.glsli"
#include "restir_di.glsli"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    restir_main(ivec2(gl_GlobalInvocationID.xy));
}
