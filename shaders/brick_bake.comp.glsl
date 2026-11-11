#version 460 core
#include "extensions.glsl"

// Thread block dimensions (# of threads per work group)
// Each brick is 729 samples, so ~12 samples per thread

// TODO- test other vals here for performance! Try 128 or 256 too
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "../include/constants.h"
#include "common.glsl"

layout(push_constant) uniform PushConstant {
    // Get GPU side data from pruning shader 
    // (C++ struct needs this EXACT order!)
    vec4 aabb_min; // Padded cube size
    PrimitivesRef prims;
    BinaryOpsRef binary_ops;
    NodesRef nodes;
    ActiveNodesRef active_nodes_out;
    IntArrayRef cells_offset;
    IntArrayRef cells_num_active;
    FloatArrayRef cell_error_out;

    // Baking variables
    IntArrayRef brick_cells_num_active; // What brick cells have surface in them or are skipped
    IntRef brick_cnt; // Counter for number of bricks (atomic)
    
    UintArrayRef  out_morton; // [dense index]
    FloatArrayRef out_aabb; // [dense index * 6]
    UintArrayRef  out_samples; // [dense index * WORDS_PER_BRICK]
    
    float voxel_size; // world units
    float band;
    int max_bricks;
    int total_num_nodes;
};

#include "eval.glsl"

shared float s_dists[BRICK_SAMPLES]; // Each SDF sampled dist in this brick
shared uint s_min_abs; // smallest abs dist value. If no samples touch SDF surface, skip brick
shared uint s_slot; // dense index (after skipping empty bricks). 0xffffffffu means skip

void main() {
    ivec3 brick = ivec3(gl_WorkGroupID); // Work group idx
    uint brick_idx = morton_encode(brick); // 3D idx -> 1D idx (spatially ordered w/ morton)

    // If there are zero CSG nodes in this brick then skip this ENTIRE work group!
    if (brick_cells_num_active.tab[brick_idx] == 0) {
        return;
    }

    // Init min on first thread of each group
    if (gl_LocalInvocationIndex == 0) {
        s_min_abs = floatBitsToUint(1e30);
    }

    barrier(); // Sync all threads!

    // Evaluate all 9^3 samples in this block!
    // TODO: Optimize here?
    for (uint s = gl_LocalInvocationIndex; s < BRICK_SAMPLES; s+=gl_WorkGroupSize.x) {
        // 1D idx (0-728) -> 3D idx (vec3(0-8))
        ivec3 brickspace_pos = ivec3(s % 9, (s / 9) % 9, s / 81);
        ivec3 voxelspace_pos = brick * BRICK_VOXELS + brickspace_pos;

        // Get SDF sample position in worldspace  
        vec3 world_pos = aabb_min.xyz + voxelspace_pos * voxel_size;

        // Find which pruning cell this sample is in
        // Pruning tree lives at 256^3. Samples are at 512^3. 
        ivec3 prunespace_pos = min(voxelspace_pos >> 1, ivec3(255)); 
        int prune_idx = int(get_cell_idx(prunespace_pos, 256)); // TODO 256->const?

        // Sample SDF!
        bool near_field;
        float d = sdf_active(world_pos, prune_idx, near_field);

        // Record
        s_dists[s] = d;
        atomicMin(s_min_abs, floatBitsToUint(abs(d)));
    }
    barrier(); // Sync all threads!

    // Decide if we keep this brick
    if (gl_LocalInvocationIndex == 0) {
        s_slot = 0xffffffffu; 
        
        // If all SDF samples are > (voxel_diag / 2), 
        // then we guarantee that the SDF surface doesn't lie within this brick!
        // (because we take the absolute value of the distance, can't check sign)
        float voxel_diag = sqrt(3.f) * voxel_size;

        if (uintBitsToFloat(s_min_abs) < 0.5 * voxel_diag) {
            // Get dense index (remove empty spots)
            uint slot = uint(atomicAdd(brick_cnt.val, 1));
            if (slot < uint(max_bricks)) {
                s_slot = slot;
            }
        }
    }
    barrier(); // Sync all threads!
    
    uint slot = s_slot; // share val w/ all threads

    // Exit each thread if brick is empty
    if (slot == 0xffffffffu) {
        return;
    }

    // Quantize to 4 samples per word
    for (uint w = gl_LocalInvocationIndex; w < WORDS_PER_BRICK; w += gl_WorkGroupSize.x) {
        uint word = 0;
        for (uint k = 0; k < 4; k++) {
            uint s = w * 4 + k;
            if (s < BRICK_SAMPLES) {
                int q = int(round(clamp(s_dists[s] / band, -1.f, 1.f) * 127.f));
                word |= (uint(q) & 0xffu) << (8 * k);
            }
        }

        // Record quantized dist to samples buffer
        out_samples.tab[slot * WORDS_PER_BRICK + w] = word;
    }

    if (gl_LocalInvocationIndex == 0) {
        // Record dense brick index
        out_morton.tab[slot] = brick_idx;

        // Record AABB of brick
        vec3 lo = aabb_min.xyz + vec3(brick) * BRICK_VOXELS * voxel_size;
        vec3 hi = lo + BRICK_VOXELS * voxel_size;
        out_aabb.tab[slot * 6 + 0] = lo.x;
        out_aabb.tab[slot * 6 + 1] = lo.y;
        out_aabb.tab[slot * 6 + 2] = lo.z;
        out_aabb.tab[slot * 6 + 3] = hi.x;
        out_aabb.tab[slot * 6 + 4] = hi.y;
        out_aabb.tab[slot * 6 + 5] = hi.z;
    }

}