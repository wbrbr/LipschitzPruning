#ifndef SDFCULLING_UTILS_H
#define SDFCULLING_UTILS_H
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>

#include <VkBootstrap.h>
#include <glm/glm.hpp>
#include "vma/vk_mem_alloc.h"
#include "constants.h"

#define _USE_MATH_DEFINES
#include <math.h>

extern int MAX_ACTIVE_COUNT;
extern int MAX_TMP_COUNT;
extern std::string spv_dir;

#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			fprintf(stderr, "%d\n", err);                           \
			abort();                                                \
		}                                                           \
	} while (0)

const int MAX_FRAMES_IN_FLIGHT = 3;

struct Init {
    GLFWwindow* window;
    vkb::Instance instance;
    vkb::InstanceDispatchTable inst_disp;
    VkSurfaceKHR surface;
    vkb::Device device;
    vkb::DispatchTable disp;
    vkb::Swapchain swapchain;
};


struct PushConstants {
    //glm::mat4 world_to_clip;
    glm::vec4 aabb_min;
    glm::vec4 aabb_max;
    glm::ivec2 resolution;
    uint64_t prims_ref;
    uint64_t binary_ops_ref;
    uint64_t nodes_ref;
    uint64_t parents_in_ref;
    uint64_t parents_out_ref;
    uint64_t active_nodes_in_ref;
    uint64_t active_nodes_out_ref;
    uint64_t cell_offsets_in_ref;
    uint64_t cell_offsets_out_ref;
    uint64_t num_active_in_ref;
    uint64_t num_active_out_ref;
    uint64_t active_count_ref;
    uint64_t cell_error_in_ref;
    uint64_t cell_error_out_ref;
    uint64_t old_to_new_scratch_ref;
    uint64_t old_to_new_count_ref;
    uint64_t tmp_ref;
    uint64_t mvp_ref;
    uint64_t cam_ref;
    int num_nodes;
    int grid_size;
    int first_lvl;
    float max_rel_err = 0;
    float viz_max;
    int shading_mode;
    float alpha;
    int culling_enabled;
    float gamma;
    int num_samples;
};

struct Buffer {
    VkBuffer buf;
    VmaAllocation alloc;
    uint64_t address;
};

struct Image {
    VkImage img;
    VmaAllocation alloc;
};

struct Pipeline {
    VkPipeline pipe;
    VkPipelineLayout layout;
};

struct RenderData {
    VmaAllocator alloc;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family;

    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    std::vector<VkImage> render_images;
    std::vector<VkImageView> render_image_views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<Image> depth_images;
    std::vector<VkImageView> depth_image_views;

    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

    Pipeline culling_pipeline;
    Pipeline eval_grid_pipeline;

    VkPipelineLayout debug_plane_pipeline_layout;
    VkPipeline debug_plane_pipeline;

    VkPipelineLayout single_block_scan_pipeline_layout;
    VkPipeline single_block_scan_pipeline;

    Pipeline fxaa_pipeline;

    VkCommandPool command_pool;
    std::vector<VkCommandBuffer> command_buffers;

    std::vector<VkSemaphore> available_semaphores;
    std::vector<VkSemaphore> finished_semaphore;
    std::vector<VkFence> in_flight_fences;
    std::vector<VkFence> image_in_flight;
    size_t current_frame = 0;

    VkDescriptorPool descriptor_pool;
    VkQueryPool query_pool;

    PushConstants push_constants;
    Buffer staging_buffer;
    Buffer prims_buffer;
    Buffer nodes_buffer;
    Buffer binary_ops_buffer;
    Buffer spheres_buffer;
    Buffer active_nodes_buffer[2];
    Buffer parents_buffer[2];
    Buffer num_active_buffer[2];
    Buffer cell_offsets_buffer[2];
    Buffer active_count_buffer;
    Buffer parents_init_buffer;
    Buffer cell_errors[2];
    Buffer active_nodes_init_buffer;
    Buffer old_to_new_scratch_buffer;
    Buffer old_to_new_count_buffer;
    Buffer tmp_buffer;
    Buffer mvp_buffer;
    Buffer cam_buffer;

    int input_idx = 0;
    int output_idx = 1;

    float culling_elapsed_ms, tracing_elapsed_ms, render_elapsed_ms, eval_grid_elapsed_ms;
    uint64_t tracing_mem_usage;
    uint64_t pruning_mem_usage;
    int max_active_count = 0;
    int max_tmp_count = 0;
    int total_num_nodes;
    int colormap_max = 25;
    glm::vec3 aabb_min = glm::vec3(-1.f);
    glm::vec3 aabb_max = glm::vec3(1);
    int final_grid_lvl = 8;
    int shading_mode = SHADING_MODE_SHADED;
    bool render_enabled = true;
    bool culling_enabled = true;
    bool hierarchy_enabled = true;
    bool eval_grid_enabled = false;
    bool show_imgui = true;
    int num_samples = 1;
    glm::vec3 cam_pos;
    float gamma = 1.2;
    bool compute_culling = true;
    glm::vec3 sphere_albedo = glm::vec3(1,0,1);
    glm::vec3 background_color = glm::vec3(1);
};

enum PrimitiveType {
    PRIMITIVE_SPHERE = 0,
    PRIMITIVE_BOX = 1,
    PRIMITIVE_CYLINDER = 2,
    PRIMITIVE_CONE = 3
};

struct SphereData {
    glm::vec4 radius;
};

struct BoxData {
    glm::vec4 sizes;
};

struct CylinderData {
    float height;
    float radius;
    float pad0;
    float pad1;
};

struct ConeData {
    float radius;
    float height;
    float pad0, pad1;
};

struct Primitive {
    union {
        SphereData sphere;
        BoxData box;
        CylinderData cylinder;
        ConeData cone;
    };
    glm::vec4 m_row0;
    glm::vec4 m_row1;
    glm::vec4 m_row2;
    glm::vec2 extrude_rounding;
    PrimitiveType type;
    float bevel;
    uint32_t color;

    float pad0, pad1, pad2;
};
static_assert(sizeof(Primitive) == 6*16);

struct BinaryOp {
    uint32_t blend_factor_and_sign;

    BinaryOp() = default;
    // 0 = union, 1 = sub, 2 = inter
    BinaryOp(float k, bool sign, uint32_t op);
};

enum NodeType {
    NODETYPE_BINARY,
    NODETYPE_PRIMITIVE,
};

struct CSGNode {
    union {
        BinaryOp binary_op;
        Primitive primitive;
    };
    NodeType type;
    int left, right;
    bool sign;
};

std::vector<char> readFile(const std::string& filename);
VkShaderModule createShaderModule(Init& init, const std::vector<char>& code, const char* debug_name);
void TransferToBuffer(const VmaAllocator& alloc, const Buffer& buffer, const void* data, int size);
void TransferFromBuffer(const VmaAllocator& alloc, const Buffer& buffer, void* data, int size);
void CopyBuffer(const RenderData& render_data, const Init& init, const Buffer& src, const Buffer& dst, int size);
void CopyImageToBuffer(const RenderData& render_data, const Init& init, VkImage src, const Buffer& dst, int width, int height);
uint64_t GetBufferAddress(const Init& init, const Buffer& buffer);
Pipeline create_compute_pipeline(Init& init, const char* shader_path, const char* shader_name, unsigned int push_constant_size);
Buffer create_buffer(Init& init, RenderData& render_data, unsigned int size, VkBufferUsageFlags usage, const char* name);

extern size_t g_memory_usage;

#endif //SDFCULLING_UTILS_H
