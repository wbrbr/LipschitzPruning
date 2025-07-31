#include "utils.h"
#include <fstream>

std::string spv_dir;

int MAX_ACTIVE_COUNT = 100 * 1000 * 1000;
int MAX_TMP_COUNT = 400 * 1000 * 1000;

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(spv_dir + "/" + filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        fprintf(stderr, "failed to open file %s\n", filename.c_str());
        abort();
    }

    size_t file_size = (size_t)file.tellg();
    std::vector<char> buffer(file_size);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(file_size));

    file.close();

    return buffer;
}

VkShaderModule createShaderModule(Init& init, const std::vector<char>& code, const char* debug_name) {
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (init.disp.createShaderModule(&create_info, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE; // failed to create shader module
    }

    VkDebugUtilsObjectNameInfoEXT name_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_SHADER_MODULE,
            .objectHandle = (uint64_t)shaderModule,
            .pObjectName = debug_name
    };
    init.disp.setDebugUtilsObjectNameEXT(&name_info);

    return shaderModule;
}

void TransferToBuffer(const VmaAllocator& alloc, const Buffer& buffer, const void* data, int size) {
    void* ptr;
    VK_CHECK(vmaMapMemory(alloc, buffer.alloc, (void**)&ptr));
    memcpy(ptr, data, size);
    vmaUnmapMemory(alloc, buffer.alloc);
}

void TransferFromBuffer(const VmaAllocator& alloc, const Buffer& buffer, void* data, int size) {
    void* ptr;
    VK_CHECK(vmaMapMemory(alloc, buffer.alloc, (void**)&ptr));
    memcpy(data, ptr, size);
    vmaUnmapMemory(alloc, buffer.alloc);
}

void CopyImageToBuffer(const RenderData& render_data, const Init& init, VkImage src, const Buffer& dst, int width, int height) {
    VK_CHECK(vkDeviceWaitIdle(init.device));
    VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
    };
    VkCommandBuffer cmd_buf = render_data.command_buffers[0];
    VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

    VkImageMemoryBarrier2 barrier = {
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
            .image = src,
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
    vkCmdPipelineBarrier2(cmd_buf, &dependency_info);

    VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { (uint32_t)width, (uint32_t)height, 1 }
    };
    vkCmdCopyImageToBuffer(cmd_buf, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.buf, 1, &copy);

    vkEndCommandBuffer(cmd_buf);

    VkCommandBufferSubmitInfo cmd_buf_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = cmd_buf,
            .deviceMask = 0
    };

    VkSubmitInfo2 submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = 0,
            .flags = 0,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = nullptr,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_buf_info,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos = nullptr
    };
    VK_CHECK(vkQueueSubmit2(render_data.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

    VK_CHECK(vkDeviceWaitIdle(init.device));

}

void CopyBuffer(const RenderData& render_data, const Init& init, const Buffer& src, const Buffer& dst, int size) {
    VK_CHECK(vkDeviceWaitIdle(init.device));
    VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
    };
    VkCommandBuffer cmd_buf = render_data.command_buffers[0];
    VK_CHECK(vkBeginCommandBuffer(cmd_buf, &begin_info));

    VkBufferCopy copy = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = (VkDeviceSize)size
    };
    vkCmdCopyBuffer(cmd_buf, src.buf, dst.buf, 1, &copy);

    vkEndCommandBuffer(cmd_buf);

    VkCommandBufferSubmitInfo cmd_buf_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = cmd_buf,
            .deviceMask = 0
    };

    VkSubmitInfo2 submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = 0,
            .flags = 0,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = nullptr,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_buf_info,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos = nullptr
    };
    VK_CHECK(vkQueueSubmit2(render_data.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

    VK_CHECK(vkDeviceWaitIdle(init.device));
}

uint64_t GetBufferAddress(const Init& init, const Buffer& buffer) {
    VkBufferDeviceAddressInfo address_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext = nullptr,
            .buffer = buffer.buf
    };
    return vkGetBufferDeviceAddress(init.device, &address_info);
}

Pipeline create_compute_pipeline(Init& init, const char* shader_path, const char* shader_name, unsigned int push_constant_size) {
    auto code = readFile(shader_path);
    VkShaderModule module = createShaderModule(init, code, shader_name);
    if (module == VK_NULL_HANDLE) abort();

    VkPushConstantRange range = {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = push_constant_size
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

    Pipeline pipeline{};
    VK_CHECK(vkCreatePipelineLayout(init.device, &layout_info, nullptr, &pipeline.layout));

    VkComputePipelineCreateInfo pipeline_info =  {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                    .module = module,
                    .pName = "main",
                    .pSpecializationInfo = nullptr
            },
            .layout = pipeline.layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1
    };
    VK_CHECK(vkCreateComputePipelines(init.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.pipe));

    return pipeline;
}

size_t g_memory_usage = 0;

Buffer create_buffer(Init& init, RenderData& data, unsigned int size, VkBufferUsageFlags usage, const char* name) {
    g_memory_usage += size;

    VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
    };
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    Buffer res{};
    VK_CHECK(vmaCreateBuffer(data.alloc, &buffer_info, &alloc_info, &res.buf, &res.alloc, nullptr));

    VkBufferDeviceAddressInfo addr_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext = nullptr,
            .buffer = res.buf
    };
    res.address = vkGetBufferDeviceAddress(init.device.device, &addr_info);

    VkDebugUtilsObjectNameInfoEXT name_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType = VK_OBJECT_TYPE_BUFFER,
            .objectHandle = (uint64_t)res.buf,
            .pObjectName = name
    };
    VK_CHECK(init.disp.setDebugUtilsObjectNameEXT(&name_info));

    return res;
}

BinaryOp::BinaryOp(float k, bool sign, unsigned int op) {
    uint32_t x = (uint32_t)sign;
    op &= 3u;
    x |= (op << 1);

    uint32_t k_bits;

    static_assert(sizeof(float) == sizeof(uint32_t));
    memcpy(&k_bits, &k, sizeof(float));

    // clear lower 3 bits
    k_bits &= ~7u;
    x |= k_bits;

    blend_factor_and_sign = x;
}