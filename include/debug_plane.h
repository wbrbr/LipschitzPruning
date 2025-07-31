#ifndef SDFCULLING_DEBUG_PLANE_H
#define SDFCULLING_DEBUG_PLANE_H
#include "utils.h"

void create_debug_plane_pipeline(Init& init, RenderData& render_data, VkPipeline& pipeline, VkPipelineLayout& pipeline_layout);
void draw_debug_plane(const Init& init, const RenderData& data, VkCommandBuffer cmd_buf);

#endif //SDFCULLING_DEBUG_PLANE_H
