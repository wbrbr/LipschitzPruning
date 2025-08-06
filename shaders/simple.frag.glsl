#version 450 core
#include "extensions.glsl"
layout (location = 0) out vec4 outColor;
layout(constant_id = 0) const int shading_mode = 0;

#include "../include/constants.h"
#include "common.glsl"

layout(push_constant) uniform PushConstant {
    //mat4 world_to_clip;
    vec4 aabb_min;
    vec4 aabb_max;
    ivec2 u_Resolution;
    PrimitivesRef prims;
    BinaryOpsRef binary_ops;
    NodesRef nodes;
    IntArrayRef parents_in;
    IntArrayRef parents_out;
    ActiveNodesRef active_nodes_in;
    ActiveNodesRef active_nodes_out;
    IntArrayRef parent_cells_offset;
    IntArrayRef cells_offset;
    IntArrayRef parent_cells_num_active;
    IntArrayRef cells_num_active;
    ivec2 pad7;
    FloatArrayRef cell_error_in;
    FloatArrayRef cell_error_out;
    ivec2 pad8;
    ivec2 pad9;
    ivec2 pad10;
    Mat4Ref mvp;
    Vec4ArrayRef cam;
    int total_num_nodes;
    int grid_size;
    int first_lvl;
    float max_rel_err;
    float viz_max;
    float alpha;
    int culling_enabled;
    float gamma;
    int num_samples;
};

#include "eval.glsl"

vec2 smin_blend( float a, float b, float k )
{
    float h = max(k-abs(a-b), 0) / k;
    float m = h*h*0.5;
    float s = m*k*0.5;
    return (a<b) ? vec2(a-s,m) : vec2(b-s,1-m);
}

vec3 get_color_active(vec3 p, int cell_idx) {
    int num_active = cells_num_active.tab[cell_idx];
    if (num_active == 0) {
        return vec3(0);
    }

    const int STACK_DEPTH = 128;

    struct StackEntry {
        float d;
        vec3 col;
    };
    StackEntry stack[STACK_DEPTH];
    int stack_idx = 0;

    int cell_offset = cells_offset.tab[cell_idx];

    for (int i = 0; i < num_active; i++) {
        ActiveNode active_node = active_nodes_out.tab[cell_offset + i];
        int node_idx = ActiveNode_index(active_node);

        Node node = nodes.tab[node_idx];
        float d;
        vec3 albedo;
        if (node.type == NODETYPE_BINARY) {
            StackEntry left_entry = stack[stack_idx-2];
            StackEntry right_entry = stack[stack_idx-1];
            float left_val = left_entry.d;
            float right_val = right_entry.d;
            stack_idx -= 2;
            BinaryOp op = binary_ops.tab[node.idx_in_type];
            float k = BinaryOp_blend_factor(op);
            float s = BinaryOp_sign(op);
            vec2 v = s*smin_blend(s*left_val, s*right_val, k);
            d = v.x;
            albedo = mix(left_entry.col, right_entry.col, v.y);
        } else if (node.type == NODETYPE_PRIMITIVE) {
            Primitive prim = prims.tab[node.idx_in_type];
            d = eval_prim(p, prim);
            float r = float((prim.color >> 0) & 0xff) / 255.f;
            float g = float((prim.color >> 8) & 0xff) / 255.f;
            float b = float((prim.color >> 16) & 0xff) / 255.f;
            albedo = vec3(r,g,b);
        }

        d *= ActiveNode_sign(active_node) ? 1 : -1;
        if (stack_idx >= STACK_DEPTH) {
            //debugPrintfEXT("Stack overflow\n");
            return vec3(0);
        }
        stack[stack_idx++] = StackEntry(d, albedo);
    }

    return stack[0].col;
}

vec3 get_color(vec3 p) {
    const int STACK_DEPTH = 128;

    struct StackEntry {
        float d;
        vec3 col;
    };
    StackEntry stack[STACK_DEPTH];
    int stack_idx = 0;

    for (int i = 0; i < total_num_nodes; i++) {
        int node_idx = i;

        Node node = nodes.tab[node_idx];
        float d;
        vec3 albedo;
        if (node.type == NODETYPE_BINARY) {
            StackEntry left_entry = stack[stack_idx-2];
            StackEntry right_entry = stack[stack_idx-1];
            float left_val = left_entry.d;
            float right_val = right_entry.d;
            stack_idx -= 2;
            BinaryOp op = binary_ops.tab[node.idx_in_type];
            float k = BinaryOp_blend_factor(op);
            float s = BinaryOp_sign(op);
            uint typ = BinaryOp_op(op);
            if (typ == OP_SUB) right_val *= -1;
            vec2 v = s*smin_blend(s*left_val, s*right_val, k);
            d = v.x;
            albedo = mix(left_entry.col, right_entry.col, v.y);
        } else if (node.type == NODETYPE_PRIMITIVE) {
            Primitive prim = prims.tab[node.idx_in_type];
            d = eval_prim(p, prim);
            float r = float((prim.color >> 0) & 0xff) / 255.f;
            float g = float((prim.color >> 8) & 0xff) / 255.f;
            float b = float((prim.color >> 16) & 0xff) / 255.f;
            albedo = vec3(r,g,b);
        }

        if (stack_idx >= STACK_DEPTH) {
            //debugPrintfEXT("Stack overflow\n");
            return vec3(0);
        }
        stack[stack_idx++] = StackEntry(d, albedo);
    }

    return stack[0].col;
}


vec3 grad_active(vec3 p, int cell_idx) {
    float h = 5e-4;
    const vec2 k = vec2(1,-1);
    bool nf;
    return normalize(k.xyy*sdf_active(p+k.xyy*h, cell_idx,nf)+
                     k.yyx*sdf_active(p+k.yyx*h, cell_idx,nf)+
                     k.yxy*sdf_active(p+k.yxy*h, cell_idx,nf)+
                     k.xxx*sdf_active(p+k.xxx*h, cell_idx,nf));
}

vec3 grad(vec3 p) {
    float h = 5e-4;
    const vec2 k = vec2(1,-1);
    bool nf;
    return normalize(k.xyy*sdf(p+k.xyy*h)+
                     k.yyx*sdf(p+k.yyx*h)+
                     k.yxy*sdf(p+k.yxy*h)+
                     k.xxx*sdf(p+k.xxx*h));
}


#if 0
float ambient_occlusion(vec3 p, vec3 N, int cell_idx) {
    float s = 0;
    float h = 5e-3;
    bool nf;
    for (int i = 1; i <= 5 ; i++) {
        float offset_dist = h * float(i);
        vec3 offset_dir = normalize(N + normalize(sin(float(i)+vec3(0,2,4))));
        s += offset_dist - sdf_active(p + offset_dir * offset_dist, cell_idx,nf);
    }
    return exp(-30*s);
}
#endif

float hash(float uv)
{
    return fract(sin(11.23 * uv) * 23758.5453);
}

#define PI 3.1415

vec3 randomSphereDir(vec2 rnd)
{
    float s = rnd.x*PI*2.;
    float t = rnd.y*2.-1.;
    return vec3(sin(s), cos(s), t) / sqrt(1.0 + t * t);
}
vec3 randomHemisphereDir(vec3 dir, float i)
{
    vec3 v = randomSphereDir( vec2(hash(i+1.), hash(i+2.)) );
    return v * sign(dot(v, dir));
}


float ambient_occlusion( in vec3 p, in vec3 n, in float maxDist, in float falloff )
{
    const int nbIte = 32;
    const float nbIteInv = 1./float(nbIte);
    const float rad = 1.-1.*nbIteInv; //Hemispherical factor (self occlusion correction)

    float ao = 0.0;

    for( int i=0; i<nbIte; i++ )
    {
        float l = hash(float(i))*maxDist;
        vec3 rd = normalize(n+randomHemisphereDir(n, l )*rad)*l; // mix direction with the normal for self occlusion problems!

        vec3 cell_size = (aabb_max.xyz - aabb_min.xyz) / float(grid_size);
        ivec3 cell = ivec3((p+rd - aabb_min.xyz) / cell_size);
        cell = clamp(cell, ivec3(0), ivec3(grid_size-1));
        int cell_idx = int(get_cell_idx(cell, grid_size));

        bool nf;
        ao += (l - max(sdf_active( p + rd, cell_idx,  nf),0.)) / maxDist * falloff;
    }

    return clamp( 1.-ao*nbIteInv, 0., 1.);
}


bool BBoxIntersect(vec3 boxMin, vec3 boxMax, vec3 r_o, vec3 r_d, out float t_inter) {
    vec3 tbot = (boxMin - r_o) / r_d;
    vec3 ttop = (boxMax - r_o) / r_d;
    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);
    vec2 t = max(tmin.xx, tmin.yz);
    float t0 = max(t.x, t.y);
    t = min(tmax.xx, tmax.yz);
    float t1 = min(t.x, t.y);
    t_inter = max(t0,0.0);
    return t1 > max(t0, 0.0);
}

bool shadow_ray_intersects_active(vec3 ray_o, vec3 ray_d, vec3 cell_size) {
    //float t = 3e-3;
    float t = 0;
    for (int i = 0; i < 2048; i++) {
        vec3 p = ray_o + t * ray_d;

        if (any(lessThan(p, aabb_min.xyz)) || any(greaterThanEqual(p, aabb_max.xyz))) {
            return false;
        }

        ivec3 cell = ivec3((p - aabb_min.xyz) / cell_size);
        cell = clamp(cell, ivec3(0), ivec3(grid_size-1));
        int cell_idx = int(get_cell_idx(cell, grid_size));

        bool near_field = true;
        float d = sdf_active(p, cell_idx, near_field);

        if (d < 1e-4) {
            return true;
        }
        t += abs(d);
    }
    return true;
}

bool shadow_ray_intersects(vec3 ray_o, vec3 ray_d, vec3 cell_size) {
    //float t = 3e-3;
    float t = 0;
    for (int i = 0; i < 2048; i++) {
        vec3 p = ray_o + t * ray_d;

        if (any(lessThan(p, aabb_min.xyz)) || any(greaterThanEqual(p, aabb_max.xyz))) {
            return false;
        }

        float d = sdf(p);

        if (d < 1e-4) {
            return true;
        }
        t += abs(d);
    }
    return true;
}


void main () { 

    vec3 cam_pos = vec3(cam.tab[0]);
    vec3 cam_target = vec3(cam.tab[1]);
    
    outColor = vec4(0);
    
    PCG pcg;
    init_pcg(pcg, uint64_t(gl_FragCoord.y) * uint64_t(u_Resolution.x) + uint64_t(gl_FragCoord.x));

    for (int sample_idx = 0; sample_idx < num_samples; sample_idx++) {
        
        float du = 0.5;
        float dv = 0.5;
        if (sample_idx != 0) {
            du = rand_float_0_1(pcg);
            dv = rand_float_0_1(pcg); 
        }


	    vec2 uv = gl_FragCoord.xy / vec2(u_Resolution);
        uv.y = 1 - uv.y;
        uv += (vec2(du,dv) * 2 - 1) * 0.5 / vec2(u_Resolution);


        vec3 forward = normalize(cam_target-cam_pos);
        vec3 right = normalize(cross(forward, vec3(0,1,0)));
        vec3 up = normalize(cross(right, forward));

        mat3 ViewToWorld = mat3(right, up, forward);
        float aspect = float(u_Resolution.x) / float(u_Resolution.y);

    #if 1
        // perspective

        vec3 ray_o = vec3(cam_pos);
	    vec3 ray_d_viewspace = normalize(vec3(0,0,1) + vec3(uv*2-1, 0));
	    ray_d_viewspace.x *= aspect;
	    ray_d_viewspace = normalize(ray_d_viewspace);
        vec3 ray_d = ViewToWorld * ray_d_viewspace;
    #else
        // orthographic

        vec3 ray_d_viewspace = vec3(0,0,1);
        vec3 ray_o_viewspace = vec3(uv * 2.0 -1.0, 0);
        ray_o_viewspace.x *= aspect;
        vec3 ray_o = vec3(cam_pos) + ViewToWorld * ray_o_viewspace;
        //vec3 ray_o = vec3(cam_pos) + ray_o_viewspace;
        //vec3 ray_d = ray_d_viewspace;
        vec3 ray_d = ViewToWorld * ray_d_viewspace;
    #endif

        gl_FragDepth = 1;

        float t = 0;
        if (!BBoxIntersect(aabb_min.xyz, aabb_max.xyz, ray_o, ray_d, t)) {
            outColor += vec4(cam.tab[3].rgb,1);
            continue;
        }
        t += 1e-4;

        vec3 cell_size = (aabb_max.xyz - aabb_min.xyz) / float(grid_size);

        for (int i = 0; i < 256; i++) {
            vec3 p = ray_o + t * ray_d;

            if (any(lessThan(p, aabb_min.xyz)) || any(greaterThanEqual(p, aabb_max.xyz))) {
                t = -1;
                break;
            }

            ivec3 cell = ivec3((p - aabb_min.xyz) / cell_size);
            cell = clamp(cell, ivec3(0), ivec3(grid_size-1));
            //if (all(greaterThanEqual(debug_cell.xyz, ivec3(0)))) {
            //    cell = debug_cell.xyz;
            //}
            int cell_idx = int(get_cell_idx(cell, grid_size));



            bool near_field = true;
            float d;
            if (bool(culling_enabled)) {
                d = sdf_active(p, cell_idx, near_field);
            } else {
                d = sdf(p);
            }

            if (d < -1e-4) {
                outColor = vec4(0,1,0,1);
                return;
            }

            if (near_field && abs(d) < min(5e-4, 5e-4*t)) {
                break;
            }
            t += abs(d);
        }
        
        vec3 color = vec3(0);
        if (t >= 0) {
            vec3 p = ray_o + t * ray_d;

            const vec4 projected_hit = mvp.m * vec4(p, 1.0);
            const float projected_depth = projected_hit.z / projected_hit.w;
        
            //gl_FragDepth = (( * projected_depth) + gl_DepthRange.near + gl_DepthRange.far) / 2.0;
            gl_FragDepth = projected_depth;

            ivec3 cell = ivec3((p - aabb_min.xyz) / cell_size);
            cell = clamp(cell, ivec3(0), ivec3(grid_size-1));
            int cell_idx = int(get_cell_idx(cell, grid_size));

            vec3 normal;
            if (bool(culling_enabled)) {
                normal = normalize(grad_active(p, cell_idx));
            } else {
                normal = normalize(grad(p));
            }
            if (shading_mode == SHADING_MODE_NORMALS) {
                color = vec3(0.5+0.5*normal);
            } else {
                vec3 L = normalize(vec3(1,1,1));
                //color = p * 0.5 + 0.5;
                vec3 albedo;
                if (bool(culling_enabled)) {
                    albedo = get_color_active(p, cell_idx);
                } else {
                    albedo = get_color(p);
                }

                // divide by 2 to get number of primitives
                int num_active = (cells_num_active.tab[cell_idx] + 1 )/ 2;
        
                if (shading_mode == SHADING_MODE_HEATMAP) {
                    albedo = inferno(min(1, float(num_active) / viz_max));
                }

                float ao;
                if (shading_mode == SHADING_MODE_BEAUTY) {
                    ao = 0.4 * ambient_occlusion(p,normal,1e-1,3);
                } else {
                    ao = 0.4;
                }

                //outColor = vec4(vec3(ao), 1);
                //return;
                // half-lambert
                //color = albedo * (dot(L,normal) * 0.5 + 0.5);
                //color = albedo * ao;
                color = albedo * ao;
                //color = vec3(dot(L,normal));

                bool in_shadow;
                if (bool(culling_enabled)) {
                    in_shadow = shadow_ray_intersects_active(p + 5e-4 * normal, L, cell_size);
                } else {
                    in_shadow = shadow_ray_intersects(p + 5e-4 * normal, L, cell_size);
                }

                if (dot(normal,L) > 0 && !in_shadow) {
                    color += albedo * dot(L,normal);
                }
            }
            //color = vec3(ao);
            //color = vec3(ambient_occlusion(p, normal, cell_idx));
            //color = normal * 0.5 + 0.5;

            //vec4 p_clip = world_to_clip * vec4(p, 1);
            //gl_FragDepth = p_clip.z / p_clip.w;
            //gl_FragDepth = 0.5;
        } else {
            color = vec3(1);
        }


        outColor += vec4 (color, 1);
    }
    outColor /= num_samples;
    outColor = vec4(pow(outColor.rgb, vec3(gamma)), 1);
}
