#[compute]
#version 460

// Software backend: two-level BVH traversal in a compute shader. Runs on any Forward+ GPU.

#define PIXEL_ID gl_GlobalInvocationID.xy

#include "common.glsli"
#include "brdfs.glsli"
#include "trace_sw.glsli"
#include "integrator.glsli"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main() {
    pt_main();
}
