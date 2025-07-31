#version 460 core
#include "extensions.glsl"

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#include "../include/constants.h"
#include "common.glsl"

layout(push_constant) uniform PushConstant {
    vec4 aabb_min;
    vec4 aabb_max;
    PrimitivesRef prims;
    BinaryOpsRef binary_ops;
    NodesRef nodes;
    ActiveNodesRef active_nodes_out;
    IntArrayRef cells_offset;
    IntArrayRef cells_num_active;
    FloatArrayRef cell_error_out;
    FloatArrayRef output_dist;
    int total_num_nodes;
    int grid_size;
    int culling_enabled;
};

#include "eval.glsl"

void main() {
    ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
    if (any(greaterThanEqual(cell, ivec3(grid_size)))) return;


    vec3 cell_size = (aabb_max - aabb_min).xyz / float(grid_size);
    int cell_idx = int(get_cell_idx(cell, grid_size));

    vec3 p = vec3(aabb_min) + cell_size * (0.5 + vec3(cell));
    bool nf;

    if (bool(culling_enabled)) {
        output_dist.tab[cell_idx] = sdf_active(p, cell_idx, nf);
    } else {
        output_dist.tab[cell_idx] = sdf(p);
    }
}