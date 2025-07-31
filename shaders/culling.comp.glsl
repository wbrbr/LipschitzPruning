#version 450
#include "extensions.glsl"

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#include "../include/constants.h"

#include "common.glsl"

layout(push_constant) uniform PushConstant {
    //mat4 world_to_clip;
    vec4 aabb_min, aabb_max;
    ivec2 u_Resolution;
    PrimitivesRef prims;
    BinaryOpsRef binary_ops;
    NodesRef nodes;
    Uint16ArrayRef parents_in;
    Uint16ArrayRef parents_out;
    ActiveNodesRef active_nodes_in;
    ActiveNodesRef active_nodes_out;
    IntArrayRef parent_cells_offset;
    IntArrayRef child_cells_offset;
    IntArrayRef parent_cells_num_active;
    IntArrayRef num_active_out;
    IntRef active_count;
    FloatArrayRef cell_value_in;
    FloatArrayRef cell_value_out;
    Uint16ArrayRef old_to_new_scratch;
    IntRef old_to_new_count;
    TmpArrayRef tmp;
    Mat4Ref mvp;
    Vec4ArrayRef cam;
    int total_num_nodes;
    int grid_size;
    int first_lvl;
    float max_rel_err;
    float viz_max;
    float alpha;
    int culling_enabled;
    uint8_t num_samples_u8;
};

#include "common_culling.glsl"

void main() {
    if (any(greaterThanEqual(gl_GlobalInvocationID.xyz, uvec3(grid_size)))) return;
    uint cell_idx = get_cell_idx(ivec3(gl_GlobalInvocationID.xyz), grid_size);

    vec3 cell_size = (aabb_max.xyz - aabb_min.xyz) / float(grid_size);
    vec3 cell_center = aabb_min.xyz + cell_size * (vec3(gl_GlobalInvocationID.xyz) + 0.5);


    int num_nodes;
    if (bool(first_lvl)) {
        num_nodes = total_num_nodes;
    } else {
        uint parent_cell_idx = get_parent_cell_idx(cell_idx, grid_size);
        num_nodes = parent_cells_num_active.tab[parent_cell_idx];
    }

    compute_pruning(cell_center, cell_size, int(cell_idx));
}
