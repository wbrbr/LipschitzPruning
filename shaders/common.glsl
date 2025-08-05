#define PRIMITIVE_SPHERE 0
#define PRIMITIVE_BOX 1
#define PRIMITIVE_CYLINDER 2
#define PRIMITIVE_CONE 3

#define NODETYPE_BINARY 0
#define NODETYPE_PRIMITIVE 1

struct Primitive {
    vec4 data;
    vec4 m_row0;
    vec4 m_row1;
    vec4 m_row2;
    vec2 extrude_rounding;
    int type;
    float bevel;
    uint color;

    float pad0, pad1, pad2;
};

struct Node {
    int type;
    int idx_in_type;
};

struct BinaryOp {
    uint blend_factor_and_sign;
};

struct ActiveNode {
    uint16_t idx_and_sign;
};

struct Tmp {
    // local state: 2 bits (0)
    // global state: 1 bit (2)
    // inactive ancestors flag: 1 bit (3)
    // sign : 1 bit (4)
    // parent: 16 bits (5)
    uint x;
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer PrimitivesRef {
    Primitive tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer NodesRef {
    Node tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer BinaryOpsRef {
    BinaryOp tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer ActiveNodesRef {
    ActiveNode tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer IntArrayRef {
    int tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer UintArrayRef {
    uint tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer Uint16ArrayRef {
    uint16_t tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer TmpArrayRef {
    Tmp tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer FloatArrayRef {
    float tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 4) buffer IntRef {
    int val;
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer Mat4Ref {
    mat4 m;
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer Vec4ArrayRef {
    vec4 tab[];
};

layout(std430, buffer_reference, buffer_reference_align = 8) buffer DebugPlaneRef {
    mat4 world_to_clip;
    vec4 farfield_color;
    float plane_y;
};

void set_bit(inout uint x, int idx) {
    x |= 1 << idx;
}

void unset_bit(inout uint x, int idx) {
    x &= ~(1 << idx);
}

bool get_bit(uint x, int idx) {
    return bool((x >> idx) & 1);
}

void write_bit(inout uint x, int idx, bool val) {
    unset_bit(x, idx);
    x |= uint(val) << idx;
}

int read_node_state(uint64_t node_state_bitfield, int idx) {
    return int((node_state_bitfield >> (2*idx)) & 3ul);
}

void write_node_state(inout uint64_t node_state, int idx, int val) {
    node_state &= ~(uint64_t(3) << (2*idx));
    node_state |= uint64_t(val) << (2*idx);
}

int Tmp_state_get(Tmp t) {
    return int(t.x & 3u);
}

bool Tmp_active_global_get(Tmp t) {
    return bool((t.x >> 2) & 1);
}

bool Tmp_inactive_ancestors_get(Tmp t) {
    return bool((t.x >> 3) & 1);
}

bool Tmp_sign_get(Tmp t) {
    return bool((t.x >> 4) & 1);
}

uint16_t Tmp_parent_get(Tmp t) {
    return uint16_t((t.x >> 5) & 0xffff);
}

void Tmp_state_write(inout Tmp t, int state) {
    t.x &= ~3u;
    t.x |= uint(state);
}

void Tmp_active_global_write(inout Tmp t, bool b) {
    write_bit(t.x, 2, b);
}

void Tmp_inactive_ancestors_write(inout Tmp t, bool b) {
    write_bit(t.x, 3, b);
}

void Tmp_sign_write(inout Tmp t, bool b) {
    write_bit(t.x, 4, b);
}

void Tmp_parent_write(inout Tmp t, uint16_t p) {
    t.x &= ~(0xffffu << 5);
    t.x |= uint(p) << 5;
}


float kernel(float x, float k) {
    if (k == 0) return 0;
    float m = max(0, k-x);
    return m*m*0.25/k;
}

#if 1
// https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
uint Part1By2(uint x)
{
  x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
  x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
  x = (x ^ (x <<  8)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
  x = (x ^ (x <<  4)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
  x = (x ^ (x <<  2)) & 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
  return x;
}

uint morton_encode(ivec3 cell)
{
    uint x = uint(cell.x);
    uint y = uint(cell.y);
    uint z = uint(cell.z);
    return (Part1By2(z) << 2) + (Part1By2(y) << 1) + Part1By2(x);
}


uint get_cell_idx(ivec3 cell, int grid_size) {
	//return cell.z * grid_size * grid_size + cell.y * grid_size + cell.x;
    return morton_encode(cell);
}

// https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
uint Compact1By2(uint x)
{
  x &= 0x09249249;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
  x = (x ^ (x >>  2)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
  x = (x ^ (x >>  4)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
  x = (x ^ (x >>  8)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
  x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
  return x;
}

ivec3 morton_decode(uint cell_idx) {
    uint x = Compact1By2(cell_idx >> 0);
    uint y = Compact1By2(cell_idx >> 1);
    uint z = Compact1By2(cell_idx >> 2);

    return ivec3(x,y,z);
}

ivec3 get_cell(uint cell_idx, int grid_size) {
    //return morton_decode(cell_idx);
    ivec3 cell;
    cell.z = int(cell_idx / (grid_size*grid_size));
    cell.y = int((cell_idx % (grid_size*grid_size)) / grid_size);
    cell.x = int(cell_idx % grid_size);
    return cell;
}

uint get_parent_cell_idx(uint cell_idx, int grid_size) {
    return cell_idx / 64;
    //ivec3 cell = get_cell(cell_idx, grid_size);
    //ivec3 parent_cell = cell / 2;
    //return get_cell_idx(parent_cell, grid_size/2);
}

float BinaryOp_blend_factor(BinaryOp op) {
    return uintBitsToFloat(op.blend_factor_and_sign & (~7u));
}

float BinaryOp_sign(BinaryOp op) {
    uint x = op.blend_factor_and_sign & 1;
    return -1. + 2. * float(x);
}

uint BinaryOp_op(BinaryOp op) {
    return (op.blend_factor_and_sign >> 1) & 3;
}

float BinaryOp_ca(BinaryOp op) {
    return 1;
}

float BinaryOp_cb(BinaryOp op) {
    return BinaryOp_op(op) == OP_SUB ? -1 : 1;
}

float BinaryOp_eval(BinaryOp op, float a, float b, float k) {
    // this common form is specific to the union, sub and inter operators that we use
    float s = BinaryOp_sign(op);
    float c_b = BinaryOp_cb(op);
    return s * min(s * a, s * c_b * b) - kernel(abs(a - c_b * b), k);
}

int ActiveNode_index(ActiveNode n) {
    return int(n.idx_and_sign & ~(1 << 15));
}

bool ActiveNode_sign(ActiveNode n) {
    return (n.idx_and_sign >> 15 == 0);
}

ActiveNode ActiveNode_make(int idx, bool sgn) {
    uint16_t v = uint16_t(idx);
    v |= uint16_t(!sgn) << 15;
    return ActiveNode(v);
}
#endif

vec3 inferno(float t) {

    const vec3 c0 = vec3(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184);
    const vec3 c1 = vec3(0.1065134194856116, 0.5639564367884091, 3.932712388889277);
    const vec3 c2 = vec3(11.60249308247187, -3.972853965665698, -15.9423941062914);
    const vec3 c3 = vec3(-41.70399613139459, 17.43639888205313, 44.35414519872813);
    const vec3 c4 = vec3(77.162935699427, -33.40235894210092, -81.80730925738993);
    const vec3 c5 = vec3(-71.31942824499214, 32.62606426397723, 73.20951985803202);
    const vec3 c6 = vec3(25.13112622477341, -12.24266895238567, -23.07032500287172);

    return c0+t*(c1+t*(c2+t*(c3+t*(c4+t*(c5+t*c6)))));
}


#define PCG u64vec2

// GPU implementation of CPU PCG
uint rand_uint32(inout PCG pcg)
{
    const uint64_t old_state = pcg.x;
    pcg.x = old_state * 6364136223846793005UL + pcg.y;

    const uint xorshifted = uint(((old_state >> 18) ^ old_state) >> 27);
    const uint rot = uint(old_state >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void init_pcg(inout PCG pcg, const uint64_t seed)
{
    pcg.x = 0;                               // m_state in CPU code
    pcg.y = (0xDA3E39CB94B95BDBUL << 1) | 1; // m_inc in CPU code
    rand_uint32(pcg);
    pcg.x += seed;
    rand_uint32(pcg);
}

float rand_float_0_1(inout PCG pcg) { return rand_uint32(pcg) * 2.32830616e-10f; }


float sdRoundBox( in vec2 p, in vec2 b, in vec4 r )
{
    r.xy = (p.x>0.0)?r.xy : r.zw;
    r.x  = (p.y>0.0)?r.x  : r.y;
    vec2 q = abs(p)-b+r.x;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x;
}


float sdExtrude( in float d, float z, in float h, float r ) {
    vec2 q = vec2(d+r, abs(z)-h);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0))-r;
}


float sdCone(vec3 position, float radius, float halfHeight) {
    vec2 p = vec2(length(position.xz) - radius, position.y + halfHeight);
    vec2 e = vec2(-radius, 2.0 * halfHeight);
    vec2 q = p - e * clamp(dot(p, e) / dot(e, e), 0.0, 1.0);
    float d = length(q);
    if (max(q.x, q.y) > 0.0) {
        return d;
    }
    return -min(d, p.y);
}

float eval_prim(vec3 p, Primitive prim) {
    mat4x3 m = transpose(mat3x4(prim.m_row0, prim.m_row1, prim.m_row2));
    p = vec3(m * vec4(p, 1));

    float dist;
    if (prim.type == PRIMITIVE_SPHERE) {
        float r = prim.data.x;
        dist = length(p) - r;
    } else if (prim.type == PRIMITIVE_BOX) {
        vec3 half_sides = prim.data.xyz * 0.5;
        float scale = max(half_sides.x, half_sides.z) * 2;
        uint corner_data = floatBitsToUint(prim.data.w);
        vec4 corner_rounding;
        corner_rounding.x = float((corner_data >> 0) & 0xff);
        corner_rounding.y = float((corner_data >> 8) & 0xff);
        corner_rounding.z = float((corner_data >> 16) & 0xff);
        corner_rounding.w = float((corner_data >> 24) & 0xff);
        corner_rounding = corner_rounding * scale / 255.f;
        corner_rounding /= 2;
        float d_2D = sdRoundBox(p.xz, half_sides.xz, corner_rounding);

        float er = p.y > 0 ? prim.extrude_rounding.x : prim.extrude_rounding.y;
        dist = sdExtrude( d_2D, p.y, half_sides.y-er, er );
    } else if (prim.type == PRIMITIVE_CYLINDER) {
        float h = prim.data.x / 2;
        float r = prim.data.y;
        vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(r,h);
        dist = min(max(d.x,d.y),0.0) + length(max(d,0.0));
    } else if (prim.type == PRIMITIVE_CONE) {
        dist = sdCone(p, prim.data.x, prim.data.y * 0.5);
    } else {
        dist = 1e20;
    }
    //dist -= prim.rounding;
    return dist;
}