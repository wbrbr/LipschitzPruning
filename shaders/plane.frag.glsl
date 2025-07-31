#version 460 core
#include "../include/constants.h"
#include "extensions.glsl"

#include "common.glsl"

layout(push_constant) uniform PushConstant{
    vec4 aabb_min;
    vec4 aabb_max;
    vec4 farfield_color;
    PrimitivesRef prims;
    BinaryOpsRef binary_ops;
    NodesRef nodes;
    ActiveNodesRef active_nodes_out;
    IntArrayRef cells_offset;
    IntArrayRef cells_num_active;
    FloatArrayRef cell_error_out;
    Mat4Ref mvp;
    int total_num_nodes;
    int grid_size;
    float plane_y;
    float viz_max;
    float plane_alpha;
};

layout(location = 0) out vec4 o_Color;
layout(location = 0) in vec3 i_Position;

#include "eval.glsl"

vec3 isoline_cmap(float d)
{
    d = max(d, 0.0);
    float c = cos(100.0f* d) > 0.25 ? 0.8 : 1.0;
    vec3 col = mix(vec3(1,1,1), vec3(0,0.5,0), d / 1.0);
    return col * vec3(c);
}

void main() {
    o_Color = vec4(1,0,0,1);

    vec3 cell_size = (aabb_max - aabb_min).xyz / float(grid_size);
    ivec3 cell = ivec3(clamp((i_Position - aabb_min.xyz) / cell_size, ivec3(0), ivec3(grid_size-1)));
    int cell_idx = int(get_cell_idx(cell, grid_size));

    int num_active = cells_num_active.tab[cell_idx];
    //vec3 col = inferno(min(float(num_active) / float(viz_max),1));
    //vec3 col = isoline_cmap(sdf(i_Position)*2);

    bool nf;
    float d_active = sdf_active(i_Position, cell_idx, nf);
    float d = sdf(i_Position);
    float err = abs(d - d_active);
    vec3 col = inferno(min(err / viz_max, 1));

    o_Color = vec4(col, 1);
}