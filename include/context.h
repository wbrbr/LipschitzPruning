#ifndef SDFCULLING_CONTEXT_H
#define SDFCULLING_CONTEXT_H
#include "utils.h"

const int WIDTH = 1920;
const int HEIGHT = 1080;

struct Timings {
    float culling_elapsed_ms;
    float tracing_elapsed_ms;
    float render_elapsed_ms;
    float eval_grid_elapsed_ms;
    float pruning_mem_usage_gb;
    float tracing_mem_usage_gb;
};

struct GPUNode {
    NodeType type;
    int idx_in_type;
};

class Context {
public:
    void initialize(bool gui, int final_grid_lvl);
    Timings render(glm::vec3 cam_position, glm::vec3 cam_target=glm::vec3(0));
    void upload(const std::vector<CSGNode>& nodes, int root_idx);
    void alloc_input_buffers(int num_nodes);

    Init init;
    RenderData render_data;
    bool gui;
    bool culling = true;
};

void create_culling_pipelines(Init& init, RenderData& render_data);
int ConvertToGPUTree(int root_idx, const std::vector<CSGNode>& csg_nodes, std::vector<GPUNode>& gpu_nodes, std::vector<Primitive>& primitives, std::vector<BinaryOp>& binary_ops, std::vector<uint16_t>& parent, std::vector<uint16_t>& active_nodes);
void UploadGPUTree(const std::vector<BinaryOp>& binary_ops, const std::vector<GPUNode>& gpu_nodes, const std::vector<Primitive>& primitives, const std::vector<uint16_t>& parent, const std::vector<uint16_t>& active_nodes, RenderData& render_data, Init& init);


#endif //SDFCULLING_CONTEXT_H
