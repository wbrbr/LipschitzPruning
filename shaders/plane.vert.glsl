#version 450

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
    

vec2 uvs[] = {
    vec2(0,0), vec2(1,0), vec2(0,1),
    vec2(0,1), vec2(1,0), vec2(1,1)
};

layout(location = 0) out vec3 o_Position;

void main() {
    vec2 uv = uvs[gl_VertexIndex];

    vec2 xz = aabb_min.xz + (aabb_max - aabb_min).xz * uv;

    vec3 p = vec3(xz.x, plane_y, xz.y);
    o_Position = p;
    gl_Position = mvp.m * vec4(p,1);
    gl_Position.y *= -1;
}