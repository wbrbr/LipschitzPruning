#include "context.h"
#include <stdio.h>
#define _USE_MATH_DEFINES
#include <cmath>

#include <memory>
#include <random>
#include <iostream>
#include <fstream>
#include <string>

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"
#include "utils.h"
#include "debug_plane.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "glm/gtc/matrix_transform.hpp"

struct EvalGridPushConstants {
    glm::vec4 aabb_min;
    glm::vec4 aabb_max;
    uint64_t prims_ref;
    uint64_t binary_ops_ref;
    uint64_t nodes_ref;
    uint64_t active_nodes_ref;
    uint64_t cells_offset_ref;
    uint64_t cells_num_active_ref;
    uint64_t cells_value_ref;
    uint64_t output_ref;
    int total_num_nodes;
    int grid_size;
    int culling_enabled;
};


size_t g_mem_usage_baseline_tracing = 0;
size_t g_mem_usage_baseline_pruning = 0;

GLFWwindow* create_window_glfw(const char* window_name = "", bool resize = true) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (!resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    return glfwCreateWindow(1024, 1024, window_name, NULL, NULL);
}

void destroy_window_glfw(GLFWwindow* window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

VkSurfaceKHR create_surface_glfw(VkInstance instance, GLFWwindow* window, VkAllocationCallbacks* allocator = nullptr) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult err = glfwCreateWindowSurface(instance, window, allocator, &surface);
    if (err) {
        const char* error_msg;
        int ret = glfwGetError(&error_msg);
        if (ret != 0) {
            std::cout << ret << " ";
            if (error_msg != nullptr) std::cout << error_msg;
            std::cout << "\n";
        }
        surface = VK_NULL_HANDLE;
    }
    return surface;
}

VkBool32 debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* userdata)
{
    fprintf(stderr, "%s\n", callback_data->pMessage);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        // REMOVE THIS FOR GPU TRACE
        abort();
    }
    return true;
}

int device_initialization(Init& init, bool gui) {
    if (gui) {
        init.window = create_window_glfw("SDF culling", true);
        if (init.window == nullptr) abort();
    }

    vkb::InstanceBuilder instance_builder;
    auto instance_ret = instance_builder.use_default_debug_messenger()
            .request_validation_layers()
            .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
            .enable_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME)
            .require_api_version(VKB_VK_API_VERSION_1_3)
            .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            .set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
            .set_debug_callback(debug_cb)
            .build();
    if (!instance_ret) {
        std::cout << instance_ret.error().message() << "\n";
        abort();
    }
    init.instance = instance_ret.value();

    init.inst_disp = init.instance.make_table();

    if (gui) {
        init.surface = create_surface_glfw(init.instance, init.window);
    } else {
        init.surface = VK_NULL_HANDLE;
    }

    VkPhysicalDeviceFeatures features{};
    features.shaderInt64 = true;
    features.shaderInt16 = true;
    features.fillModeNonSolid = true;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.storageBuffer16BitAccess = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.bufferDeviceAddress = true;
    features12.hostQueryReset = true;
    features12.storagePushConstant8 = true;
    features12.shaderFloat16 = true;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.synchronization2 = true;
    features13.dynamicRendering = true;

    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR pipeline_exec_properties_features{};
    pipeline_exec_properties_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR;
    pipeline_exec_properties_features.pNext = nullptr;
    pipeline_exec_properties_features.pipelineExecutableInfo = true;

    vkb::PhysicalDeviceSelector phys_device_selector(init.instance);
    if (gui) phys_device_selector.set_surface(init.surface);
    auto phys_device_ret = phys_device_selector
            .set_required_features_11(features11)
            .set_required_features_12(features12)
            .set_required_features_13(features13)
            .set_required_features(features)
            .allow_any_gpu_device_type(false)
            .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
            .add_required_extension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)
            .add_required_extension("VK_KHR_pipeline_executable_properties")
            .add_required_extension_features(pipeline_exec_properties_features)
            .require_present(false)
            .select(vkb::DeviceSelectionMode::only_fully_suitable);
    if (!phys_device_ret) {
        std::cout << phys_device_ret.error().message() << "\n";
        abort();
    }
    vkb::PhysicalDevice physical_device = phys_device_ret.value();
    printf("%s\n", physical_device.name.c_str());

    vkb::DeviceBuilder device_builder{ physical_device };
    auto device_ret = device_builder.build();
    if (!device_ret) {
        std::cout << device_ret.error().message() << "\n";
        abort();
    }
    init.device = device_ret.value();

    init.disp = init.device.make_table();

    return 0;
}

int create_swapchain(Init& init, RenderData& data) {

    vkb::SwapchainBuilder swapchain_builder{ init.device };
    auto swap_ret = swapchain_builder.set_old_swapchain(init.swapchain).set_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT).set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR).set_required_min_image_count(MAX_FRAMES_IN_FLIGHT).build();
    if (!swap_ret) {
        std::cout << swap_ret.error().message() << " " << swap_ret.vk_result() << "\n";
        return -1;
    }
    vkb::destroy_swapchain(init.swapchain);
    init.swapchain = swap_ret.value();

    data.swapchain_images = init.swapchain.get_images().value();
    data.swapchain_image_views = init.swapchain.get_image_views().value();
    data.swapchain_images.resize(MAX_FRAMES_IN_FLIGHT);
    data.swapchain_image_views.resize(MAX_FRAMES_IN_FLIGHT);
    return 0;
}

void create_render_images(Init& init, RenderData& data, bool gui) {
    int n = MAX_FRAMES_IN_FLIGHT;
    if (!gui) {
        init.swapchain.image_count = n;
        init.swapchain.extent.width = WIDTH;
        init.swapchain.extent.height = HEIGHT;
        init.swapchain.image_format = VK_FORMAT_R8G8B8A8_SRGB;
    }

    data.render_images.resize(n);
    data.render_image_views.resize(n);

    VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .extent = { init.swapchain.extent.width, init.swapchain.extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &data.graphics_queue_family,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    for (size_t i = 0; i < n; i++) {
        VmaAllocation img_alloc;
        VK_CHECK(vmaCreateImage(data.alloc, &image_info, &alloc_info, &data.render_images[i], &img_alloc, nullptr));
    }

    for (size_t i = 0; i < n; i++) {
        VkImageViewCreateInfo view_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = data.render_images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = image_info.format,
                .components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        VK_CHECK(vkCreateImageView(init.device, &view_info, nullptr, &data.render_image_views[i]));
    }
}

int get_queues(Init& init, RenderData& data) {
    auto gq = init.device.get_queue(vkb::QueueType::graphics);
    if (!gq.has_value()) {
        std::cout << "failed to get graphics queue: " << gq.error().message() << "\n";
        return -1;
    }
    data.graphics_queue = gq.value();
    data.graphics_queue_family = init.device.get_queue_index(vkb::QueueType::graphics).value();

    auto pq = init.device.get_queue(vkb::QueueType::present);
    if (pq.has_value()) {
        data.present_queue = pq.value();
    }
    return 0;
}

int create_render_pass(Init& init, RenderData& data) {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = VK_FORMAT_R8G8B8A8_SRGB;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment = {
            .flags = 0,
            .format = VK_FORMAT_D24_UNORM_S8_UINT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { color_attachment, depth_attachment };

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = sizeof(attachments) / sizeof(attachments[0]);
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (init.disp.createRenderPass(&render_pass_info, nullptr, &data.render_pass) != VK_SUCCESS) {
        std::cout << "failed to create render pass\n";
        return -1; // failed to create render pass!
    }
    return 0;
}

static void print_pipeline_stats(Init& init, VkPipeline pipeline, uint32_t executable_idx) {
    VkPipelineExecutableInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
        .pNext = nullptr,
        .pipeline = pipeline,
        .executableIndex = executable_idx
    };
    VkPipelineExecutableStatisticKHR stats[128];
    uint32_t num_stats = 128;
    for (int i = 0; i < num_stats; i++) {
        stats[i] = {};
        stats[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
        stats[i].pNext = nullptr;
    }
    VK_CHECK(init.disp.getPipelineExecutableStatisticsKHR(&info, &num_stats, stats));

    for (uint32_t i = 0; i < num_stats; i++) {
        printf("%s\n%s: ", stats[i].description, stats[i].name);
        switch (stats[i].format) {
            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                if (stats[i].value.b32) { printf("TRUE"); } else { printf("FALSE"); }
                break;

            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                printf("%li", stats[i].value.i64);
                break;

            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                printf("%lu", stats[i].value.u64);
                break;

            case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                printf("%lf", stats[i].value.f64);
                break;

            default:
                abort();
        }
        printf("\n\n");
    }
}


int create_graphics_pipeline(Init& init, RenderData& data) {
    auto vert_code = readFile("vert.spv");
    auto frag_code = readFile("frag.spv");

    VkShaderModule vert_module = createShaderModule(init, vert_code, "simple.vert.glsl");
    VkShaderModule frag_module = createShaderModule(init, frag_code, "simple.frag.glsl");
    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        std::cout << "failed to create shader module\n";
        return -1; // failed to create shader modules
    }

    VkPipelineShaderStageCreateInfo vert_stage_info = {};
    vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage_info.module = vert_module;
    vert_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage_info = {};
    frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage_info.module = frag_module;
    frag_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage_info, frag_stage_info };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 0;
    vertex_input_info.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)init.swapchain.extent.width;
    viewport.height = (float)init.swapchain.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = init.swapchain.extent;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &colorBlendAttachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    VkPushConstantRange range = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(PushConstants)
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &range;

    if (init.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &data.pipeline_layout) != VK_SUCCESS) {
        std::cout << "failed to create pipeline layout\n";
        return -1; // failed to create pipeline layout
    }

    std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_info = {};
    dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_info.pDynamicStates = dynamic_states.data();

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
            .front = {},
            .back = {},
            .minDepthBounds = 0,
            .maxDepthBounds = 0
    };

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_info;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.layout = data.pipeline_layout;
    pipeline_info.renderPass = data.render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;

    if (init.disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data.graphics_pipeline) != VK_SUCCESS) {
        std::cout << "failed to create pipline\n";
        return -1; // failed to create graphics pipeline
    }

    {
        puts("=== Graphics pipeline statistics ===");
        print_pipeline_stats(init, data.graphics_pipeline, 1);
    }

    init.disp.destroyShaderModule(frag_module, nullptr);
    init.disp.destroyShaderModule(vert_module, nullptr);
    return 0;
}


Pipeline create_culling_pipeline(Init& init, RenderData& render_data, const char* shader_path, const char* debug_name) {
    auto code = readFile("culling.comp.spv");
    VkShaderModule module = createShaderModule(init, code, "culling.comp.glsl");
    if (module == VK_NULL_HANDLE) abort();

    VkPushConstantRange range = {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(PushConstants)
    };
    VkPipelineLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range
    };

    Pipeline pipeline;
    VK_CHECK(vkCreatePipelineLayout(init.device, &layout_info, nullptr, &pipeline.layout));

    VkComputePipelineCreateInfo pipeline_info =  {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR,
            .stage = {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                    .module = module,
                    .pName = "main",
            },
            .layout = pipeline.layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
    };
    VK_CHECK(vkCreateComputePipelines(init.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.pipe));

    {
        puts("=== Culling pipeline statistics ===");
        print_pipeline_stats(init, pipeline.pipe, 0);
    }

    return pipeline;
}

void create_culling_pipelines(Init& init, RenderData& render_data) {
    render_data.culling_pipeline = create_culling_pipeline(init, render_data, "culling.comp.spv", "culling.comp.glsl");
}

void create_depth_buffers(Init& init, RenderData& data) {
    int n = (int)data.render_images.size();
    data.depth_images.resize(n);
    data.depth_image_views.resize(n);

    VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D24_UNORM_S8_UINT,
            .extent = { init.swapchain.extent.width, init.swapchain.extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &data.graphics_queue_family,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    for (size_t i = 0; i < n; i++) {
        VK_CHECK(vmaCreateImage(data.alloc, &image_info, &alloc_info, &data.depth_images[i].img, &data.depth_images[i].alloc, nullptr));
    }

    for (size_t i = 0; i < n; i++) {
        VkImageViewCreateInfo view_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = data.depth_images[i].img,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = image_info.format,
                .components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        VK_CHECK(vkCreateImageView(init.device, &view_info, nullptr, &data.depth_image_views[i]));
    }
}

int create_framebuffers(Init& init, RenderData& data) {

    create_depth_buffers(init, data);

    data.framebuffers.resize(data.render_images.size());

    for (size_t i = 0; i < data.render_image_views.size(); i++) {
        VkImageView attachments[] = { data.render_image_views[i], data.depth_image_views[i] };

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = data.render_pass;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = init.swapchain.extent.width;
        framebuffer_info.height = init.swapchain.extent.height;
        framebuffer_info.layers = 1;

        if (init.disp.createFramebuffer(&framebuffer_info, nullptr, &data.framebuffers[i]) != VK_SUCCESS) {
            return -1; // failed to create framebuffer
        }
    }
    return 0;
}

int create_command_pool(Init& init, RenderData& data) {
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = init.device.get_queue_index(vkb::QueueType::graphics).value();
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (init.disp.createCommandPool(&pool_info, nullptr, &data.command_pool) != VK_SUCCESS) {
        std::cout << "failed to create command pool\n";
        return -1; // failed to create command pool
    }
    return 0;
}

int create_command_buffers(Init& init, RenderData& data) {
    data.command_buffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = data.command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)data.command_buffers.size();

    if (init.disp.allocateCommandBuffers(&allocInfo, data.command_buffers.data()) != VK_SUCCESS) {
        return -1; // failed to allocate command buffers;
    }

#if 0
    for (size_t i = 0; i < data.command_buffers.size(); i++) {
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (init.disp.beginCommandBuffer(data.command_buffers[i], &begin_info) != VK_SUCCESS) {
            return -1; // failed to begin recording command buffer
        }

        VkRenderPassBeginInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = data.render_pass;
        render_pass_info.framebuffer = data.framebuffers[i];
        render_pass_info.renderArea.offset = { 0, 0 };
        render_pass_info.renderArea.extent = init.swapchain.extent;
        VkClearValue clearColor{ { { 0.0f, 0.0f, 0.0f, 1.0f } } };
        render_pass_info.clearValueCount = 2;
        render_pass_info.pClearValues = clearValues;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)init.swapchain.extent.width;
        viewport.height = (float)init.swapchain.extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = init.swapchain.extent;

        init.disp.cmdSetViewport(data.command_buffers[i], 0, 1, &viewport);
        init.disp.cmdSetScissor(data.command_buffers[i], 0, 1, &scissor);

        init.disp.cmdBeginRenderPass(data.command_buffers[i], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

        init.disp.cmdBindPipeline(data.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);

        init.disp.cmdPushConstants(data.command_buffers[i], data.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &data.push_constants);

        init.disp.cmdDraw(data.command_buffers[i], 3, 1, 0, 0);

        init.disp.cmdEndRenderPass(data.command_buffers[i]);

        if (init.disp.endCommandBuffer(data.command_buffers[i]) != VK_SUCCESS) {
            std::cout << "failed to record command buffer\n";
            return -1; // failed to record command buffer!
        }
    }
#endif
    return 0;
}

int create_sync_objects(Init& init, RenderData& data) {
    data.available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    data.finished_semaphore.resize(MAX_FRAMES_IN_FLIGHT);
    data.in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);
    data.image_in_flight.resize(init.swapchain.image_count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.available_semaphores[i]) != VK_SUCCESS ||
            init.disp.createSemaphore(&semaphore_info, nullptr, &data.finished_semaphore[i]) != VK_SUCCESS ||
            init.disp.createFence(&fence_info, nullptr, &data.in_flight_fences[i]) != VK_SUCCESS) {
            std::cout << "failed to create sync objects\n";
            return -1; // failed to create synchronization objects for a frame
        }
    }
    return 0;
}

int recreate_swapchain(Init& init, RenderData& data) {
    init.disp.deviceWaitIdle();

    init.disp.destroyCommandPool(data.command_pool, nullptr);

    for (auto framebuffer : data.framebuffers) {
        init.disp.destroyFramebuffer(framebuffer, nullptr);
    }

    {
        int width = 0;
        int height = 0;
        // wait while window is minimized
        glfwGetFramebufferSize(init.window, &width, &height);
        while (width == 0 && height == 0) {
            glfwGetFramebufferSize(init.window, &width, &height);
            glfwWaitEvents();
        }
        init.disp.deviceWaitIdle();
    }

    init.swapchain.destroy_image_views(data.swapchain_image_views);

    if (0 != create_swapchain(init, data)) return -1;
    create_render_images(init, data, true);
    if (0 != create_framebuffers(init, data)) return -1;
    if (0 != create_command_pool(init, data)) return -1;
    if (0 != create_command_buffers(init, data)) return -1;
    return 0;
}

void pipeline_barrier(VkCommandBuffer cmd_buf, VkPipelineStageFlagBits2 src_stage_mask, VkAccessFlagBits2 src_access_mask, VkPipelineStageFlagBits2 dst_stage_mask, VkAccessFlagBits2 dst_access_mask) {
    VkMemoryBarrier2 barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = src_stage_mask,
            .srcAccessMask = src_access_mask,
            .dstStageMask = dst_stage_mask,
            .dstAccessMask = dst_access_mask
    };
    VkDependencyInfo dependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
    };
    vkCmdPipelineBarrier2(cmd_buf, &dependency);
}

void set_push_constants(RenderData& data, int grid_lvl, bool first_lvl) {
    data.push_constants.grid_size = 1 << grid_lvl;
    data.push_constants.first_lvl = first_lvl;
    data.push_constants.active_nodes_in_ref = data.active_nodes_buffer[data.input_idx].address;
    data.push_constants.active_nodes_out_ref = data.active_nodes_buffer[data.output_idx].address;
    data.push_constants.parents_in_ref = data.parents_buffer[data.input_idx].address;
    data.push_constants.parents_out_ref = data.parents_buffer[data.output_idx].address;
    data.push_constants.cell_offsets_in_ref = data.cell_offsets_buffer[data.input_idx].address;
    data.push_constants.cell_offsets_out_ref = data.cell_offsets_buffer[data.output_idx].address;
    data.push_constants.num_active_in_ref = data.num_active_buffer[data.input_idx].address;
    data.push_constants.num_active_out_ref = data.num_active_buffer[data.output_idx].address;
    data.push_constants.cell_error_in_ref = data.cell_errors[data.input_idx].address;
    data.push_constants.cell_error_out_ref = data.cell_errors[data.output_idx].address;
    data.push_constants.old_to_new_count_ref = data.old_to_new_count_buffer.address + grid_lvl * sizeof(int);
    data.push_constants.active_count_ref = data.active_count_buffer.address + grid_lvl * sizeof(int);
    data.push_constants.aabb_min = glm::vec4(data.aabb_min, 0);
    data.push_constants.aabb_max = glm::vec4(data.aabb_max, 0);
    data.push_constants.viz_max = (float)data.colormap_max;
    data.push_constants.shading_mode = data.shading_mode;
    data.push_constants.mvp_ref = data.mvp_buffer.address;
    data.push_constants.culling_enabled = data.culling_enabled;
    data.push_constants.num_samples = (uint8_t)data.num_samples;
    data.push_constants.cam_ref = data.cam_buffer.address;
    data.push_constants.gamma = data.gamma;
}

int draw_frame(Init& init, RenderData& data, bool gui) {
    init.disp.waitForFences(1, &data.in_flight_fences[data.current_frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index = 0;
    if (gui) {
        VkResult result = init.disp.acquireNextImageKHR(
                init.swapchain, UINT64_MAX, data.available_semaphores[data.current_frame], VK_NULL_HANDLE, &image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            return recreate_swapchain(init, data);
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            std::cout << "failed to acquire swapchain image. Error " << result << "\n";
            return -1;
        }
    } else {
        image_index = data.current_frame;
    }


    if (data.image_in_flight[image_index] != VK_NULL_HANDLE) {
        init.disp.waitForFences(1, &data.image_in_flight[image_index], VK_TRUE, UINT64_MAX);
    }
    data.image_in_flight[image_index] = data.in_flight_fences[data.current_frame];


    {
        int i = image_index;

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(init.disp.beginCommandBuffer(data.command_buffers[i], &begin_info));

        vkResetQueryPool(init.device, data.query_pool, 0, 128);

        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, data.query_pool, 4);

        VkBufferCopy region = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = data.push_constants.num_nodes * sizeof(uint16_t)
        };
        vkCmdCopyBuffer(data.command_buffers[i], data.parents_init_buffer.buf, data.parents_buffer[data.input_idx].buf, 1, &region);
        vkCmdCopyBuffer(data.command_buffers[i], data.active_nodes_init_buffer.buf, data.active_nodes_buffer[data.input_idx].buf, 1, &region);
        vkCmdFillBuffer(data.command_buffers[i], data.active_count_buffer.buf, 0, 10 * sizeof(int), 0);
        vkCmdFillBuffer(data.command_buffers[i], data.old_to_new_count_buffer.buf, 0, 10 * sizeof(int), 0);
#if FAR_FIELD_VIZ
        assert(false); // check that grid size is 256
        vkCmdFillBuffer(data.command_buffers[i], data.cell_errors[0].buf, 0, 256 * 256 * 256 * sizeof(float), 0);
        vkCmdFillBuffer(data.command_buffers[i], data.cell_errors[1].buf, 0, 256 * 256 * 256 * sizeof(float), 0);
#endif
        pipeline_barrier(data.command_buffers[i], VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);

        // CULLING
        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, data.query_pool, 0);
        bool first_lvl = true;
       
        set_push_constants(data, data.final_grid_lvl, first_lvl);
       
        if (data.culling_enabled && data.compute_culling) {
            int initial_grid_lvl = data.hierarchy_enabled ? 2 : data.final_grid_lvl;
            for (int grid_lvl = initial_grid_lvl; grid_lvl <= data.final_grid_lvl; grid_lvl += 2) {
                //vkCmdFillBuffer(data.command_buffers[i], data.active_count_buffer.buf, grid_lvl*sizeof(int), sizeof(int), 0);
                //vkCmdFillBuffer(data.command_buffers[i], data.old_to_new_count_buffer.buf, grid_lvl*sizeof(int), sizeof(int), 0);
                pipeline_barrier(data.command_buffers[i], VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                set_push_constants(data, grid_lvl, first_lvl);

                int num_groups = (data.push_constants.grid_size + 3) / 4;

                vkCmdBindPipeline(data.command_buffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, data.culling_pipeline.pipe);
                vkCmdPushConstants(data.command_buffers[i], data.culling_pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &data.push_constants);
                vkCmdDispatch(data.command_buffers[i], num_groups, num_groups, num_groups);

                pipeline_barrier(data.command_buffers[i], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

                first_lvl = false;
                if (grid_lvl != data.final_grid_lvl) std::swap(data.input_idx, data.output_idx);
            }
        }


        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, data.query_pool, 1);

        pipeline_barrier(data.command_buffers[i], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        {

            VkImageMemoryBarrier2 barriers[] = {
                {
                   .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                   .pNext = nullptr,
                   .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                   .srcAccessMask = VK_ACCESS_2_NONE,
                   .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                   .image = data.render_images[i],
                   .subresourceRange = {
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1
                   }
                },
                {
                   .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                   .pNext = nullptr,
                   .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                   .srcAccessMask = VK_ACCESS_2_NONE,
                   .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                   .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                   .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                   .image = data.depth_images[i].img,
                   .subresourceRange = {
                           .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1
                   }
                }
            };
            VkDependencyInfo dependency_info = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .pNext = nullptr,
                    .dependencyFlags = 0,
                    .memoryBarrierCount = 0,
                    .pMemoryBarriers = nullptr,
                    .bufferMemoryBarrierCount = 0,
                    .pBufferMemoryBarriers = nullptr,
                    .imageMemoryBarrierCount = 2,
                    .pImageMemoryBarriers = barriers
            };
            vkCmdPipelineBarrier2(data.command_buffers[i], &dependency_info);
        }


        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)init.swapchain.extent.width;
        viewport.height = (float)init.swapchain.extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = init.swapchain.extent;

        init.disp.cmdSetViewport(data.command_buffers[i], 0, 1, &viewport);
        init.disp.cmdSetScissor(data.command_buffers[i], 0, 1, &scissor);


        VkRenderingAttachmentInfo color_attachment_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = data.render_image_views[i],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = {},
            .resolveImageLayout = {},
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = { 1,1,1,1 } } }
        };
        VkRenderingAttachmentInfo depth_attachment_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = data.depth_image_views[i],
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .resolveImageView = VK_NULL_HANDLE,
                .resolveImageLayout = {},
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.depthStencil = { 1, 0 } }
        };

        VkRenderingInfo rendering_info = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderArea = { { 0, 0}, init.swapchain.extent},
                .layerCount = 1,
                .viewMask = 0,
                .colorAttachmentCount = 1,
                .pColorAttachments = &color_attachment_info,
                .pDepthAttachment = &depth_attachment_info,
                .pStencilAttachment = nullptr
        };

        vkCmdBeginRendering(data.command_buffers[i], &rendering_info);


#if 0
        static bool draw_plane = false;
        if (gui) {
            ImGui::Checkbox("Draw plane", &draw_plane);
        }
        if (draw_plane) {
            draw_debug_plane(init, data, data.command_buffers[i]);
        }
#endif

        vkCmdEndRendering(data.command_buffers[i]);

        VkRenderPassBeginInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = data.render_pass;
        render_pass_info.framebuffer = data.framebuffers[i];
        render_pass_info.renderArea.offset = { 0, 0 };
        render_pass_info.renderArea.extent = init.swapchain.extent;
        VkClearValue clear_values[] = {
                { .color = { 0.f, 0.f, 0.f, 1.f }},
                { .depthStencil = { .depth = 1.f, .stencil = 0}}
        };
        render_pass_info.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]);
        render_pass_info.pClearValues = clear_values;


        init.disp.cmdBeginRenderPass(data.command_buffers[i], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

        init.disp.cmdBindPipeline(data.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);
        set_push_constants(data, data.final_grid_lvl, false);
        init.disp.cmdPushConstants(data.command_buffers[i], data.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &data.push_constants);

        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, data.query_pool, 2);
        if (data.render_enabled) {
            init.disp.cmdDraw(data.command_buffers[i], 3, 1, 0, 0);
        }
        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, data.query_pool, 3);

        init.disp.cmdEndRenderPass(data.command_buffers[i]);

        if (gui) {
            ImGui::End();
            ImGui::Render();
            rendering_info.pDepthAttachment = nullptr;
            color_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            vkCmdBeginRendering(data.command_buffers[i], &rendering_info);
            if (data.show_imgui) {
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), data.command_buffers[i]);
            }
            vkCmdEndRendering(data.command_buffers[i]);
        }

        if (gui)
        {
            VkImageMemoryBarrier2 barriers[] = {
                    {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = data.render_images[i],
                        .subresourceRange = {
                                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = 0,
                                .levelCount = 1,
                                .baseArrayLayer = 0,
                                .layerCount = 1
                        }
                    },
                    {
                            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                            .pNext = nullptr,
                            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image = data.swapchain_images[i],
                            .subresourceRange = {
                                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = 0,
                                    .levelCount = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount = 1
                            }
                    }
            };
            VkDependencyInfo  dependency_info = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .pNext = nullptr,
                    .dependencyFlags = 0,
                    .memoryBarrierCount = 0,
                    .pMemoryBarriers = nullptr,
                    .bufferMemoryBarrierCount = 0,
                    .pBufferMemoryBarriers = nullptr,
                    .imageMemoryBarrierCount = 2,
                    .pImageMemoryBarriers = barriers
            };
            vkCmdPipelineBarrier2(data.command_buffers[i], &dependency_info);
        }

        {
            VkImageSubresourceLayers subresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
            };
            VkOffset3D start = { 0, 0, 0 };
            VkOffset3D end = { (int)init.swapchain.extent.width, (int)init.swapchain.extent.height, 1 };
            VkImageBlit blit = {
                   .srcSubresource = subresource,
                   .srcOffsets = { start, end },
                   .dstSubresource = subresource,
                   .dstOffsets = { start, end }
            };
            if (gui) {
                //vkCmdCopyImage(data.command_buffers[i], data.render_images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.swapchain_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                vkCmdBlitImage(data.command_buffers[i], data.render_images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, data.swapchain_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
            }
        }

        if (gui)
        {
            VkImageMemoryBarrier2 barrier = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .pNext = nullptr,
                    .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = data.swapchain_images[i],
                    .subresourceRange = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1
                    }
            };
            VkDependencyInfo dependency_info = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .pNext = nullptr,
                    .dependencyFlags = 0,
                    .memoryBarrierCount = 0,
                    .pMemoryBarriers = nullptr,
                    .bufferMemoryBarrierCount = 0,
                    .pBufferMemoryBarriers = nullptr,
                    .imageMemoryBarrierCount = 1,
                    .pImageMemoryBarriers = &barrier
            };
            vkCmdPipelineBarrier2(data.command_buffers[i], &dependency_info);
        }

        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, data.query_pool, 6);
        if (data.eval_grid_enabled)
        {
            EvalGridPushConstants eval_grid_push_constants = {
                .aabb_min = glm::vec4(data.aabb_min,0),
                .aabb_max = glm::vec4(data.aabb_max,0),
                .prims_ref = data.prims_buffer.address,
                .binary_ops_ref = data.binary_ops_buffer.address,
                .nodes_ref = data.nodes_buffer.address,
                .active_nodes_ref = data.active_nodes_buffer[data.input_idx].address,
                .cells_offset_ref = data.cell_offsets_buffer[data.input_idx].address,
                .cells_num_active_ref = data.num_active_buffer[data.input_idx].address,
                .cells_value_ref = data.cell_errors[data.input_idx].address,
                .output_ref = data.tmp_buffer.address,
                .total_num_nodes = data.push_constants.num_nodes,
                .grid_size = 1 << data.final_grid_lvl,
                .culling_enabled = data.culling_enabled
            };
            vkCmdBindPipeline(data.command_buffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, data.eval_grid_pipeline.pipe);
            vkCmdPushConstants(data.command_buffers[i], data.eval_grid_pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EvalGridPushConstants), &eval_grid_push_constants);
            int grid_size = 1 << data.final_grid_lvl;
            int num_groups = (grid_size + 3) / 4;
            vkCmdDispatch(data.command_buffers[i], num_groups, num_groups, num_groups);
        }
        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, data.query_pool, 7);

        vkCmdWriteTimestamp(data.command_buffers[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, data.query_pool, 5);

        if (init.disp.endCommandBuffer(data.command_buffers[i]) != VK_SUCCESS) {
            std::cout << "failed to record command buffer\n";
            return -1; // failed to record command buffer!
        }
    }



    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = { data.available_semaphores[data.current_frame] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    if (gui) {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = wait_semaphores;
        submitInfo.pWaitDstStageMask = wait_stages;
    } else {
        submitInfo.waitSemaphoreCount = 0;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;
    }

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &data.command_buffers[image_index];

    VkSemaphore signal_semaphores[] = { data.finished_semaphore[data.current_frame] };
    if (gui) {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signal_semaphores;
    } else {
        submitInfo.signalSemaphoreCount = 0;
        submitInfo.pSignalSemaphores = nullptr;
    }

    init.disp.resetFences(1, &data.in_flight_fences[data.current_frame]);

    if (init.disp.queueSubmit(data.graphics_queue, 1, &submitInfo, data.in_flight_fences[data.current_frame]) != VK_SUCCESS) {
        std::cout << "failed to submit draw command buffer\n";
        return -1; //"failed to submit draw command buffer
    }

    if (gui) {
        VkPresentInfoKHR present_info = {};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;

        VkSwapchainKHR swapChains[] = { init.swapchain };
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapChains;

        present_info.pImageIndices = &image_index;

        VkResult result = init.disp.queuePresentKHR(data.present_queue, &present_info);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            return recreate_swapchain(init, data);
        }
        else if (result != VK_SUCCESS) {
            std::cout << "failed to present swapchain image\n";
            return -1;
        }
    }

    data.current_frame = (data.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkDeviceWaitIdle(init.device));

    std::vector<uint64_t> timestamps(8);
    //TODO: don't wait for results
    VK_CHECK(vkGetQueryPoolResults(init.device, data.query_pool, 0, timestamps.size(), timestamps.size() * sizeof(timestamps[0]), timestamps.data(), sizeof(timestamps[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(init.device.physical_device, &props);
    float period = props.limits.timestampPeriod;
    data.culling_elapsed_ms = (float)(timestamps[1]-timestamps[0]) * period / 1000000.0f;
    data.tracing_elapsed_ms = (float)(timestamps[3]-timestamps[2]) * period / 1000000.0f;
    data.render_elapsed_ms = (float)(timestamps[5]-timestamps[4]) * period / 1000000.0f;
    data.eval_grid_elapsed_ms = (float)(timestamps[7] - timestamps[6]) * period / 1000000.0f;

    //g_timings[g_frame] = data.render_elapsed_ms;

    // TODO: don't sync
    vkDeviceWaitIdle(init.device);
    CopyBuffer(data, init, data.active_count_buffer, data.staging_buffer, 10*sizeof(int));
    std::vector<int> active_counts(10);
    TransferFromBuffer(data.alloc, data.staging_buffer, active_counts.data(), active_counts.size()*sizeof(active_counts[0]));

    std::vector<int> tmp_counts(10);
    CopyBuffer(data, init, data.old_to_new_count_buffer, data.staging_buffer, 10 * sizeof(int));
    TransferFromBuffer(data.alloc, data.staging_buffer, tmp_counts.data(), tmp_counts.size() * sizeof(tmp_counts[0]));

    data.pruning_mem_usage = 0;
    data.max_tmp_count = 0;
    data.max_active_count = 0;
    data.tracing_mem_usage = 0;
    for (int i = 2; i <= data.final_grid_lvl; i += 2) {
        uint64_t pruning_mem_usage = g_mem_usage_baseline_pruning + (uint64_t)active_counts[i] * 4 * sizeof(uint16_t)
            + (uint64_t)tmp_counts[i] * (sizeof(uint16_t) + sizeof(uint32_t));
        uint64_t tracing_mem_usage = 
        data.pruning_mem_usage = std::max(data.pruning_mem_usage, pruning_mem_usage);
        data.max_tmp_count = std::max(data.max_tmp_count, tmp_counts[i]);
        data.max_active_count = std::max(data.max_active_count, active_counts[i]);
    }
    data.tracing_mem_usage = g_mem_usage_baseline_tracing + 2 * (uint64_t)active_counts[data.final_grid_lvl] * sizeof(uint16_t);

    return 0;
}

void cleanup(Init& init, RenderData& data) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        init.disp.destroySemaphore(data.finished_semaphore[i], nullptr);
        init.disp.destroySemaphore(data.available_semaphores[i], nullptr);
        init.disp.destroyFence(data.in_flight_fences[i], nullptr);
    }

    init.disp.destroyCommandPool(data.command_pool, nullptr);

    for (auto framebuffer : data.framebuffers) {
        init.disp.destroyFramebuffer(framebuffer, nullptr);
    }

    init.disp.destroyPipeline(data.graphics_pipeline, nullptr);
    init.disp.destroyPipelineLayout(data.pipeline_layout, nullptr);
    init.disp.destroyRenderPass(data.render_pass, nullptr);

    init.swapchain.destroy_image_views(data.swapchain_image_views);

    vkb::destroy_swapchain(init.swapchain);
    vkb::destroy_device(init.device);
    vkb::destroy_surface(init.instance, init.surface);
    vkb::destroy_instance(init.instance);
    destroy_window_glfw(init.window);
}


#if 0
// returns stack size needed to evaluate the subtree
int MakeLeftHeavy(int idx, std::vector<CSGNode>& csg_nodes) {
    const CSGNode& node = csg_nodes[idx];
    switch (node.type) {
    case NODETYPE_BINARY:
        int num_left = MakeLeftHeavy(node.left, csg_nodes);
        int num_right = MakeLeftHeavy(node.right, csg_nodes);

        if (num_left == num_right) {
            // 0,0 -> no stack needed
            if (num_left == 0) {
                return 0;
            }
            return num_left + 1;
        }
        else if (num_left < num_right) {
            return num_left + 1;
        }
        break;
    case NODETYPE_SPHERE:
        return 0;
        break;
    default:
        abort();
    }
}
#endif

int ConvertToGPUTree(int root_idx, const std::vector<CSGNode>& csg_nodes, std::vector<GPUNode>& gpu_nodes, std::vector<Primitive>& primitives, std::vector<BinaryOp>& binary_ops, std::vector<uint16_t>& parent, std::vector<uint16_t>& active_nodes) {

    std::vector<int> cpu_to_gpu(csg_nodes.size());
    std::vector<int> stack = { root_idx };
    std::vector<int> preorder;
    while (!stack.empty()) {
        int current_idx = stack.back();
        stack.pop_back();

        preorder.push_back(current_idx);
        if (csg_nodes[current_idx].type == NODETYPE_BINARY) {
            stack.push_back(csg_nodes[current_idx].left);
            stack.push_back(csg_nodes[current_idx].right);
        }
    }
    assert(preorder.size() == csg_nodes.size());
    active_nodes.resize(csg_nodes.size());
    for (int i = (int)csg_nodes.size() - 1; i >= 0; i--) {
        int current_idx = preorder[i];
        const CSGNode& node = csg_nodes[current_idx];

        switch (node.type) {
            case NODETYPE_BINARY:
            {
                int gpu_left = cpu_to_gpu[node.left];
                int gpu_right = cpu_to_gpu[node.right];
                binary_ops.push_back(node.binary_op);
                GPUNode gpu_node = {
                        .type = node.type,
                        .idx_in_type = (int)binary_ops.size() - 1,
                };
                gpu_nodes.push_back(gpu_node);
                parent[gpu_left] = (uint16_t)gpu_nodes.size() - 1;
                parent[gpu_right] = (uint16_t)gpu_nodes.size() - 1;
                parent.push_back(0xffff);
                break;
            }
            case NODETYPE_PRIMITIVE:
            {
                primitives.push_back(node.primitive);
                GPUNode gpu_node = {
                        .type = node.type,
                        .idx_in_type = (int)primitives.size() - 1,
                };
                gpu_nodes.push_back(gpu_node);
                parent.push_back(0xffff);
                break;
            }
            default:
                abort();
        }
        uint32_t gpu_idx = gpu_nodes.size()-1;
        cpu_to_gpu[current_idx] = gpu_idx;
        active_nodes[gpu_idx] = gpu_idx;
        if (!node.sign) {
            active_nodes[gpu_idx] |= 1 << 15;
        }
    }

    return cpu_to_gpu[root_idx];
}


void init_imgui(Init& init, RenderData& render_data) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    if (!ImGui_ImplGlfw_InitForVulkan(init.window, true)) {
        abort();
    }

    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;

    ImGui_ImplVulkan_InitInfo init_info = {
            .Instance = init.instance,
            .PhysicalDevice = init.device.physical_device,
            .Device = init.device.device,
            .QueueFamily = render_data.graphics_queue_family,
            .Queue = render_data.graphics_queue,
            .DescriptorPool = render_data.descriptor_pool,
            .RenderPass = VK_NULL_HANDLE,
            .MinImageCount = 2,
            .ImageCount = 2,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
            .PipelineCache = VK_NULL_HANDLE,
            .Subpass = 0,
            .UseDynamicRendering = true,
            .PipelineRenderingCreateInfo = {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                    .pNext = nullptr,
                    .viewMask = 0,
                    .colorAttachmentCount = 1,
                    .pColorAttachmentFormats = &format,
                    .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
                    .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
            },
    };
    if (!ImGui_ImplVulkan_Init(&init_info)) abort();
}

void create_descriptor_pool(Init& init, RenderData& render_data) {
    VkDescriptorPoolSize pool_size[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 }
    };
    VkDescriptorPoolCreateInfo descriptor_pool_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 128,
            .poolSizeCount = 1,
            .pPoolSizes = pool_size
    };
    VK_CHECK(vkCreateDescriptorPool(init.device, &descriptor_pool_info, nullptr, &render_data.descriptor_pool));
}

void create_query_pool(Init& init, RenderData& render_data) {
    VkQueryPoolCreateInfo query_pool_info = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 128,
            .pipelineStatistics = 0
    };
    VK_CHECK(vkCreateQueryPool(init.device, &query_pool_info, nullptr, &render_data.query_pool));
}

void UploadGPUTree(const std::vector<BinaryOp>& binary_ops, const std::vector<GPUNode>& gpu_nodes, const std::vector<Primitive>& primitives, const std::vector<uint16_t>& parent, const std::vector<uint16_t>& active_nodes, RenderData& render_data, Init& init)
{
    if (!primitives.empty()) {
        TransferToBuffer(render_data.alloc, render_data.staging_buffer, primitives.data(), primitives.size() * sizeof(primitives[0]));
        CopyBuffer(render_data, init, render_data.staging_buffer, render_data.prims_buffer, primitives.size() * sizeof(primitives[0]));
    }

    {
        TransferToBuffer(render_data.alloc, render_data.staging_buffer, gpu_nodes.data(), gpu_nodes.size() * sizeof(gpu_nodes[0]));
        CopyBuffer(render_data, init, render_data.staging_buffer, render_data.nodes_buffer, gpu_nodes.size() * sizeof(gpu_nodes[0]));
    }

    if (!binary_ops.empty()) {
        TransferToBuffer(render_data.alloc, render_data.staging_buffer, binary_ops.data(), binary_ops.size() * sizeof(binary_ops[0]));
        CopyBuffer(render_data, init, render_data.staging_buffer, render_data.binary_ops_buffer, binary_ops.size() * sizeof(binary_ops[0]));
    }

    if (!primitives.empty()) {
        TransferToBuffer(render_data.alloc, render_data.staging_buffer, primitives.data(), primitives.size() * sizeof(primitives[0]));
        CopyBuffer(render_data, init, render_data.staging_buffer, render_data.prims_buffer, primitives.size() * sizeof(primitives[0]));
    }

    {
        TransferToBuffer(render_data.alloc, render_data.staging_buffer, parent.data(),
            parent.size() * sizeof(parent[0]));
        CopyBuffer(render_data, init, render_data.staging_buffer, render_data.parents_init_buffer,
            parent.size() * sizeof(parent[0]));
    }

    {

        TransferToBuffer(render_data.alloc, render_data.staging_buffer, active_nodes.data(), active_nodes.size() * sizeof(active_nodes[0]));
        CopyBuffer(render_data, init, render_data.staging_buffer, render_data.active_nodes_init_buffer, active_nodes.size() * sizeof(active_nodes[0]));
    }
    vkDeviceWaitIdle(init.device);
}

void UploadScene(const std::vector<CSGNode>& csg_tree, int root_idx, Init& init, RenderData& render_data) {

    std::vector<BinaryOp> binary_ops;
    std::vector<GPUNode> gpu_tree;
    std::vector<Primitive> primitives;
    std::vector<uint16_t> parent;
    std::vector<uint16_t> active_nodes;
    int gpu_root_idx = ConvertToGPUTree(root_idx, csg_tree, gpu_tree, primitives, binary_ops, parent, active_nodes);

    UploadGPUTree(binary_ops, gpu_tree, primitives, parent, active_nodes, render_data, init);
}

void UploadAnim(const std::vector<std::vector<CSGNode>>& csg_trees, const std::vector<int>& root_indices, Init& init, RenderData& render_data) {
    int num_frames = csg_trees.size();

    std::vector<std::vector<GPUNode>> gpu_trees(num_frames);
    std::vector<int> gpu_root_indices(num_frames);
    std::vector<std::vector<Primitive>> prims(num_frames);
    std::vector<std::vector<BinaryOp>> binary_ops(num_frames);
    std::vector<std::vector<uint16_t>> parents(num_frames);
    std::vector<std::vector<uint16_t>> active_nodes(num_frames);
    for (int i = 0; i < num_frames; i++) {
        gpu_root_indices[i] = ConvertToGPUTree(root_indices[i], csg_trees[i], gpu_trees[i], prims[i], binary_ops[i], parents[i], active_nodes[i]);
    }
}


void Context::initialize(bool gui, int final_grid_lvl) {
    this->gui = gui;
    render_data = {};

    if (0 != device_initialization(init, gui)) abort();
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(init.device.physical_device, &props);
    if (sizeof(PushConstants) > props.limits.maxPushConstantsSize) {
        fprintf(stderr, "Push constant size is above the limit\n");
        abort();
    }

    {
        VmaVulkanFunctions vk_funcs{};
        vk_funcs.vkGetInstanceProcAddr = init.inst_disp.fp_vkGetInstanceProcAddr;
        VmaAllocatorCreateInfo alloc_info = {
                .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
                .physicalDevice = init.device.physical_device,
                .device = init.device.device,
                .preferredLargeHeapBlockSize = false,
                .pAllocationCallbacks = nullptr,
                .pDeviceMemoryCallbacks = nullptr,
                .pHeapSizeLimit = nullptr,
                .pVulkanFunctions = &vk_funcs,
                .instance = init.instance,
                .vulkanApiVersion = VK_API_VERSION_1_3,
                .pTypeExternalMemoryHandleTypes = nullptr
        };
        VK_CHECK(vmaCreateAllocator(&alloc_info, &render_data.alloc));
    }

    if (gui) {
        if (0 != create_swapchain(init, render_data)) abort();
    }
    if (0 != get_queues(init, render_data)) abort();
    if (0 != create_command_pool(init, render_data)) abort();
    if (0 != create_command_buffers(init, render_data)) abort();
    create_render_images(init, render_data, gui);
    create_descriptor_pool(init, render_data);
    if (gui) init_imgui(init, render_data);
    if (0 != create_render_pass(init, render_data)) abort();
    if (0 != create_graphics_pipeline(init, render_data)) abort();
    render_data.push_constants.max_rel_err = 1;
    create_culling_pipelines(init, render_data);
    create_debug_plane_pipeline(init, render_data, render_data.debug_plane_pipeline, render_data.debug_plane_pipeline_layout);
    render_data.eval_grid_pipeline = create_compute_pipeline(init, "dense_eval.comp.spv", "dense_eval.comp.glsl", sizeof(EvalGridPushConstants));
    if (0 != create_framebuffers(init, render_data)) abort();
    if (0 != create_sync_objects(init, render_data)) abort();
    create_query_pool(init, render_data);

    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    render_data.mvp_buffer = create_buffer(init, render_data, sizeof(glm::mat4), buffer_usage, "mvp_buffer");
    render_data.cam_buffer = create_buffer(init, render_data, sizeof(glm::vec4)*4, buffer_usage, "cam_buffer");
    int s = 1 << final_grid_lvl;
    int num_cells = s*s*s;
    render_data.num_active_buffer[0] = create_buffer(init, render_data, num_cells*sizeof(int), buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "num_active_buffer[0]");
    render_data.cell_offsets_buffer[0] = create_buffer(init, render_data, num_cells*sizeof(int), buffer_usage, "cell_offsets_buffer[0]");
    render_data.cell_errors[0] = create_buffer(init, render_data, num_cells*sizeof(float), buffer_usage, "cell_errors[0]");
    g_mem_usage_baseline_tracing = g_memory_usage;

    render_data.active_count_buffer = create_buffer(init, render_data, 10 * sizeof(int), buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "active_count_buffer");
    render_data.cell_errors[1] = create_buffer(init, render_data, num_cells * sizeof(float), buffer_usage, "cell_errors[1]");
    render_data.num_active_buffer[1] = create_buffer(init, render_data, num_cells * sizeof(int), buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "num_active_buffer[1]");
    render_data.cell_offsets_buffer[1] = create_buffer(init, render_data, num_cells * sizeof(int), buffer_usage, "cell_offsets_buffer[1]");
    render_data.old_to_new_count_buffer = create_buffer(init, render_data, 10 * sizeof(int), buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "old_to_new_count_buffer");
    g_mem_usage_baseline_pruning = g_memory_usage;

    render_data.old_to_new_scratch_buffer = create_buffer(init, render_data, MAX_TMP_COUNT*sizeof(uint16_t), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "old_to_new_scratch_buffer");
    render_data.tmp_buffer = create_buffer(init, render_data, MAX_TMP_COUNT*sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT|buffer_usage, "tmp_buffer");
    render_data.active_nodes_buffer[0] = create_buffer(init, render_data, MAX_ACTIVE_COUNT*sizeof(uint16_t), buffer_usage, "active_nodes_buffer[0]");
    render_data.active_nodes_buffer[1] = create_buffer(init, render_data, MAX_ACTIVE_COUNT*sizeof(uint16_t), buffer_usage, "active_nodes_buffer[1]");
    render_data.parents_buffer[0] = create_buffer(init, render_data, MAX_ACTIVE_COUNT * sizeof(uint16_t), buffer_usage, "parents_buffer[0]");
    render_data.parents_buffer[1] = create_buffer(init, render_data, MAX_ACTIVE_COUNT * sizeof(uint16_t), buffer_usage, "parents_buffer[1]");

    {
        VkBufferCreateInfo buffer_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .size = 4 * 4096 * 4096,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        VK_CHECK(vmaCreateBuffer(render_data.alloc, &buffer_info, &alloc_info, &render_data.staging_buffer.buf, &render_data.staging_buffer.alloc, nullptr));
    }

    render_data.input_idx = 0;
    render_data.output_idx = 1;
}

void Context::alloc_input_buffers(int num_nodes) {
    if (render_data.nodes_buffer.address) {
        vmaDestroyBuffer(render_data.alloc, render_data.nodes_buffer.buf, render_data.nodes_buffer.alloc);
        vmaDestroyBuffer(render_data.alloc, render_data.binary_ops_buffer.buf, render_data.binary_ops_buffer.alloc);
        vmaDestroyBuffer(render_data.alloc, render_data.prims_buffer.buf, render_data.prims_buffer.alloc);
        vmaDestroyBuffer(render_data.alloc, render_data.parents_init_buffer.buf, render_data.parents_init_buffer.alloc);
        vmaDestroyBuffer(render_data.alloc, render_data.active_nodes_init_buffer.buf, render_data.active_nodes_init_buffer.alloc);
    }
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    render_data.nodes_buffer = create_buffer(init, render_data, num_nodes * sizeof(GPUNode), buffer_usage, "nodes_buffer");
    render_data.binary_ops_buffer = create_buffer(init, render_data, (num_nodes/2) * sizeof(GPUNode), buffer_usage, "binary_ops_buffer");
    render_data.prims_buffer = create_buffer(init, render_data, num_nodes*sizeof(Primitive), buffer_usage, "prims_buffer");
    render_data.parents_init_buffer = create_buffer(init, render_data, num_nodes * sizeof(uint16_t), buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "parents_init_buffer");
    render_data.active_nodes_init_buffer = create_buffer(init, render_data, num_nodes * sizeof(uint16_t), buffer_usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "active_nodes_init");
}


Timings Context::render(glm::vec3 cam_position, glm::vec3 cam_target) {
    //render_data.input_idx = 0;
    //render_data.output_idx = 1;
    //render_data.push_constants.world_to_clip = proj_mat * view_mat;
    //render_data.push_constants.cam_pos = glm::vec4(0,0,1,0);
    render_data.push_constants.resolution = { init.swapchain.extent.width, init.swapchain.extent.height };
    render_data.push_constants.num_nodes = render_data.total_num_nodes;
    render_data.push_constants.prims_ref = GetBufferAddress(init, render_data.prims_buffer);
    render_data.push_constants.binary_ops_ref = GetBufferAddress(init, render_data.binary_ops_buffer);
    render_data.push_constants.nodes_ref = GetBufferAddress(init, render_data.nodes_buffer);
    render_data.push_constants.active_nodes_in_ref = GetBufferAddress(init, render_data.active_nodes_buffer[0]);
    render_data.push_constants.parents_in_ref = GetBufferAddress(init, render_data.parents_buffer[0]);
    render_data.push_constants.active_nodes_out_ref = GetBufferAddress(init, render_data.active_nodes_buffer[1]);
    render_data.push_constants.parents_out_ref = GetBufferAddress(init, render_data.parents_buffer[1]);
    render_data.push_constants.num_active_in_ref = GetBufferAddress(init, render_data.num_active_buffer[0]);
    render_data.push_constants.num_active_out_ref = GetBufferAddress(init, render_data.num_active_buffer[1]);
    render_data.push_constants.cell_offsets_in_ref = render_data.cell_offsets_buffer[render_data.input_idx].address;
    render_data.push_constants.cell_offsets_out_ref = render_data.cell_offsets_buffer[render_data.output_idx].address;
    render_data.push_constants.active_count_ref = GetBufferAddress(init, render_data.active_count_buffer);
    render_data.push_constants.old_to_new_scratch_ref = render_data.old_to_new_scratch_buffer.address;
    render_data.push_constants.old_to_new_count_ref = render_data.old_to_new_count_buffer.address;
    render_data.push_constants.tmp_ref = render_data.tmp_buffer.address;
    int grid_dim_log2 = 8;
    render_data.push_constants.grid_size = 1 << grid_dim_log2;
    render_data.push_constants.first_lvl = true;

    glm::mat4 view_mat = glm::lookAt(cam_position, glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 proj_mat = glm::perspective((float)M_PI / 2.f, (float)init.swapchain.extent.width / (float)init.swapchain.extent.height, 0.01f, 10.f);
    glm::mat4 mvp = proj_mat * view_mat;
    TransferToBuffer(render_data.alloc, render_data.staging_buffer, &mvp[0], sizeof(mvp));
    CopyBuffer(render_data, init, render_data.staging_buffer, render_data.mvp_buffer, sizeof(mvp));

    render_data.cam_pos = cam_position;

    glm::vec4 cam_data[4];
    cam_data[0] = glm::vec4(cam_position,0);
    cam_data[1] = glm::vec4(cam_target, 0);
    cam_data[2] = glm::vec4(render_data.sphere_albedo, 1);
    cam_data[3] = glm::vec4(render_data.background_color, 1);
    TransferToBuffer(render_data.alloc, render_data.staging_buffer, cam_data, sizeof(cam_data));
    CopyBuffer(render_data, init, render_data.staging_buffer, render_data.cam_buffer, sizeof(cam_data));

    draw_frame(init, render_data, gui);

    return {
        .culling_elapsed_ms = render_data.culling_elapsed_ms,
        .tracing_elapsed_ms = render_data.tracing_elapsed_ms,
        .render_elapsed_ms = render_data.render_elapsed_ms,
        .eval_grid_elapsed_ms = render_data.eval_grid_elapsed_ms,
        .pruning_mem_usage_gb = (float)((double)render_data.pruning_mem_usage / (double)(1024. * 1024. * 1024.)),
        .tracing_mem_usage_gb = (float)((double)render_data.tracing_mem_usage / (double)(1024.*1024.*1024.))
    };
}

void Context::upload(const std::vector<CSGNode> &nodes, int root_idx) {
    UploadScene(nodes, root_idx, init, render_data);
    render_data.total_num_nodes = (int)nodes.size();
}