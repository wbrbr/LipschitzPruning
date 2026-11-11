const int OP_UNION = 0;
const int OP_SUB = 1;
const int OP_INTER = 2;

const int SHADING_MODE_SHADED = 0;
const int SHADING_MODE_HEATMAP = 1;
const int SHADING_MODE_NORMALS = 2;
const int SHADING_MODE_BEAUTY = 3;

const int BRICK_VOXELS = 8; // Number of voxels per brick on each axis
const int BRICK_SAMPLES = 729; // 9 corners to sample per brick axis
const int WORDS_PER_BRICK = 184; // (729 bytes per brick + 7 padding) / 4 int8 per word
const int MAX_BRICKS = 131072; // Cap on output brick bake buffer