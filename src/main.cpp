#include "context.h"
//#include "example_config.h"
#include <string>
#include "CLI/CLI.hpp"
#include <random>
#include <queue>
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"
#include "scene.h"
#include <filesystem>

int create_scene(std::vector<CSGNode>& csg_tree, const std::string& input_path, glm::vec3& aabb_min, glm::vec3& aabb_max) {
    csg_tree.clear();
    load_json(input_path.c_str(), csg_tree, aabb_min, aabb_max);
    int root_idx = 0;
    return root_idx;
}

float cam_distance = 3.f;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    cam_distance -= yoffset * 0.1;
}

bool show_imgui = true;

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    if (key == GLFW_KEY_I && action == GLFW_PRESS) {
        show_imgui = !show_imgui;
    }
}

int main(int argc, char** argv) {
    glm::vec3 cam_target = glm::vec3(0);
    float cam_yaw = 0.f;
    float cam_pitch = M_PI / 2;

    bool culling_enabled = true;
    int num_samples = 1;
    std::string shading_mode_str = "shaded";

    std::string anim_path = "";
    //std::string input_file = "../build/catalog/guy.json";
    std::string input_file = "../scenes/trees.json";
    spv_dir = ".";
    CLI::App cli{ "Lipschitz Pruning demo" };
    cli.add_option("-i,--input", input_file, "Input");
    cli.add_option("-s,--shaders", spv_dir, "SPIR-V path");
    float min_coord = -1;
    float max_coord = 1;
    cli.add_option("--min", min_coord, "AABB min");
    cli.add_option("--max", max_coord, "AABB min");
    cli.add_option("--cam_yaw", cam_yaw, "Camera yaw");
    cli.add_option("--cam_pitch", cam_pitch, "Camera pitch");
    cli.add_option("--cam_dist", cam_distance, "Camera pitch");
    cli.add_option("--culling", culling_enabled, "Enable culling");
    cli.add_option("--samples", num_samples, "Samples per pixel");
    cli.add_option("--shading", shading_mode_str, "Shading mode");
    cli.add_option("--max-active", MAX_ACTIVE_COUNT, "Max active count");
    cli.add_option("--max-tmp", MAX_TMP_COUNT, "Max tmp count");
    cli.add_option("--anim", anim_path, "Animation directory");
    cli.add_option("--target_x", cam_target.x, "Target X");
    cli.add_option("--target_y", cam_target.y, "Target Y");
    cli.add_option("--target_z", cam_target.z, "Target Z");
    CLI11_PARSE(cli, argc, argv);

    //std::string input_file = "C:\\Users\\schtr\\Documents\\projects\\SDFCulling\\build\\catalog\\guy.json";
    //spv_dir = "C:\\Users\\schtr\\Documents\\projects\\SDFCulling\\build";

    constexpr int NUM_SCENES = 3;
    const char* preset_scenes[NUM_SCENES+1][2] = {
        { "Trees", "trees.json" },
        { "Monument", "monument.json" },
        { "Molecule", "molecule.json" },
        { "Custom", nullptr }
    };
    int preset_scene_idx = 0;

    int num_nodes = 0;
    std::vector<CSGNode> csg_tree;



    Context ctx;
    ctx.initialize(true, 8);

    int root_idx = create_scene(csg_tree, input_file, ctx.render_data.aabb_min, ctx.render_data.aabb_max);
    num_nodes = csg_tree.size();

    ctx.alloc_input_buffers(num_nodes);
    ctx.upload(csg_tree, root_idx);

    ctx.render_data.push_constants.alpha = 1;
    ctx.render_data.culling_enabled = culling_enabled;
    ctx.render_data.num_samples = num_samples;

    if (shading_mode_str == "normals") {
        ctx.render_data.shading_mode = SHADING_MODE_NORMALS;
    }
    else if (shading_mode_str == "shaded") {
        ctx.render_data.shading_mode = SHADING_MODE_SHADED;
    }
    else if (shading_mode_str == "beauty") {
        ctx.render_data.shading_mode = SHADING_MODE_BEAUTY;
    }
    else {
        fprintf(stderr, "Unknown shading mode: %s\n", shading_mode_str.c_str());
        abort();
    }

    glfwSetKeyCallback(ctx.init.window, key_callback);
    glfwSetScrollCallback(ctx.init.window, scroll_callback);

    double last_x, last_y;
    glfwGetCursorPos(ctx.init.window, &last_x, &last_y);

    Timings timing = { };

    double anim_start_time;
    float anim_speed = 4;
    bool anim_play = false;
    while (!glfwWindowShouldClose(ctx.init.window)/* && g_frame < 10000*/) {
        glfwPollEvents();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("GUI");

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginCombo("Preset", preset_scenes[preset_scene_idx][0])) {
            for (int i = 0; i < NUM_SCENES; i++) {
                bool is_selected = (i == preset_scene_idx);
                if (ImGui::Selectable(preset_scenes[i][0], is_selected)) {
                    preset_scene_idx = i;
                    input_file = "../scenes/" + std::string(preset_scenes[i][1]);
                    root_idx = create_scene(csg_tree, input_file, ctx.render_data.aabb_min, ctx.render_data.aabb_max);
                    num_nodes = csg_tree.size();
                    ctx.alloc_input_buffers(num_nodes);
                    ctx.upload(csg_tree, root_idx);

                    anim_play = false;
                    ctx.render_data.culling_enabled = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reload scene")) {
            std::vector<CSGNode> csg_tree;
            int root_idx = create_scene(csg_tree, input_file, ctx.render_data.aabb_min, ctx.render_data.aabb_max);
            ctx.upload(csg_tree, root_idx);
        }
        ImGui::SliderFloat3("AABB min", &ctx.render_data.aabb_min[0], -3, 0);
        ImGui::SliderFloat3("AABB max", &ctx.render_data.aabb_max[0], 0, 3);
        ImGui::Text("Num nodes: %d", num_nodes);

        ImGui::SeparatorText("Animation");
        if (anim_play) {
            if (ImGui::Button("Stop anim")) {
                anim_play = false;
                root_idx = create_scene(csg_tree, input_file, ctx.render_data.aabb_min, ctx.render_data.aabb_max);
                num_nodes = csg_tree.size();
                ctx.alloc_input_buffers(num_nodes);
                ctx.upload(csg_tree, root_idx);
            }
        } else if (ImGui::Button("Play anim")) {
            anim_play = true;
            anim_start_time = glfwGetTime();
            {
                CSGNode node{};
                node.primitive.sphere = { .radius = glm::vec4(0.2) };
                node.primitive.m_row0 = glm::vec4(1,0,0,0);;
                node.primitive.m_row1 = glm::vec4(0,1,0,0);;
                node.primitive.m_row2 = glm::vec4(0,0,1,0);;
                node.primitive.type = PRIMITIVE_SPHERE;
                node.primitive.color = 0xaaaaff;
                node.type = NODETYPE_PRIMITIVE;
                node.left = -1;
                node.right = -1;
                node.sign = true;
                csg_tree.push_back(node);
            }
            {
                CSGNode node{};
                node.binary_op = BinaryOp(1e-1, true, OP_UNION);
                node.type = NODETYPE_BINARY;
                node.sign = true;
                node.left = root_idx;
                node.right = csg_tree.size()-1;
                csg_tree.push_back(node);
            }
            num_nodes = csg_tree.size();
            root_idx = csg_tree.size()-1;
            ctx.alloc_input_buffers(num_nodes);
            ctx.upload(csg_tree, root_idx);
        }
        ImGui::SliderFloat("Anim speed", &anim_speed, 0, 4.f);
        if (anim_play) {
            auto before = std::chrono::high_resolution_clock::now();
            float anim_time = (float)(glfwGetTime() - anim_start_time);
            anim_time *= anim_speed;
            glm::vec3 center = {
                cosf(anim_time),
                cosf(anim_time*0.3f),
                sinf(anim_time)};
            center *= sin(anim_time*0.56 + 123.4);
            float radius = 0.2;
            glm::vec3 scale = ctx.render_data.aabb_max-ctx.render_data.aabb_min-2.f*radius;
            center = ctx.render_data.aabb_min + radius + (center * 0.5f + 0.5f) * scale;
            csg_tree[num_nodes-2].primitive.m_row0[3] = -center.x;
            csg_tree[num_nodes-2].primitive.m_row1[3] = -center.y;
            csg_tree[num_nodes-2].primitive.m_row2[3] = -center.z;
            ctx.upload(csg_tree, root_idx);
            auto after = std::chrono::high_resolution_clock::now();
            float upload_ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(after - before).count() / (float)1000.f;
            ImGui::Text("Upload time: %fms\n", upload_ms);
        }

        ImGui::SeparatorText("Pruning");
        if (num_nodes > 500) {
             ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Enable pruning", &ctx.render_data.culling_enabled);
        if (num_nodes > 500) {
            ImGui::SameLine();
            ImGui::Text("(forced when > 500 nodes)");
            ImGui::EndDisabled();
        }
        ImGui::Checkbox("Recompute pruning", &ctx.render_data.compute_culling);
        if (ImGui::Button("-")) {
            ctx.render_data.final_grid_lvl -= 2;
            if (ctx.render_data.final_grid_lvl < 2) ctx.render_data.final_grid_lvl = 2;
        }
        ImGui::SameLine();
        if (ImGui::Button("+")) {
            ctx.render_data.final_grid_lvl += 2;
            if (ctx.render_data.final_grid_lvl > 8) ctx.render_data.final_grid_lvl = 8;
        }
        ImGui::SameLine(); ImGui::Text("Grid size: 1 << %d\n", ctx.render_data.final_grid_lvl);


        ctx.render_data.show_imgui = show_imgui;



        glm::vec3 v = glm::vec3{
            cam_distance * sinf(cam_yaw) * sinf(cam_pitch),
            cam_distance * cosf(cam_pitch),
            cam_distance * cosf(cam_yaw) * sinf(cam_pitch),
        };
        glm::vec3 cam_position = cam_target + v;

        double cur_x, cur_y;
        glfwGetCursorPos(ctx.init.window, &cur_x, &cur_y);
        double delta_x = cur_x - last_x;
        double delta_y = cur_y - last_y;
        last_x = cur_x;
        last_y = cur_y;

        if (!ImGui::GetIO().WantCaptureMouse && glfwGetMouseButton(ctx.init.window, GLFW_MOUSE_BUTTON_LEFT)) {
            cam_yaw -= 0.01 * delta_x;
            cam_pitch -= 0.01 * delta_y;
            cam_yaw = fmodf(cam_yaw, 2.f * M_PI);
            cam_pitch = fminf(fmaxf(cam_pitch, 1e-3), M_PI - 1e-3);
        }

        if (!ImGui::GetIO().WantCaptureMouse && glfwGetMouseButton(ctx.init.window, GLFW_MOUSE_BUTTON_RIGHT)) {
            glm::vec3 vel = glm::vec3(0);

            glm::vec3 right = glm::normalize(glm::cross(v, glm::vec3(0, 1, 0)));
            glm::vec3 cam_up = glm::normalize(glm::cross(right, v));
            vel += right * 0.01f * (float)delta_x;
            vel += cam_up * 0.01f * (float)delta_y;
            cam_target += vel;
            cam_position += vel;
        }


        //ImGui::ShowDemoWindow();
        ImGui::SeparatorText("Timings");
        ImGui::Text("Render: %fms", ctx.render_data.render_elapsed_ms);
        ImGui::Text("Culling: %fms", ctx.render_data.culling_elapsed_ms);
        ImGui::Text("Tracing: %fms", ctx.render_data.tracing_elapsed_ms);

        ImGui::SeparatorText("VRAM");
        //ImGui::Text("Memory usage: %lfG", (double)g_memory_usage / (1024. * 1024. * 1024.));
        {
            VkPhysicalDeviceMemoryBudgetPropertiesEXT budget;
            budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
            budget.pNext = nullptr;
            VkPhysicalDeviceMemoryProperties2 props;
            props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            props.pNext = &budget;
            vkGetPhysicalDeviceMemoryProperties2(ctx.init.device.physical_device, &props);

            for (int i = 0; i < props.memoryProperties.memoryHeapCount; i++) {
                if ((props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) continue;
                ImGui::Text("Heap %i: %lfG / %lfG", i, (double)budget.heapUsage[i] / (1024.*1024.*1024.), (double)budget.heapBudget[i] / (1024.*1024.*1024.));
            }
        }
        ImGui::Text("Actual mem usage: %fG", timing.pruning_mem_usage_gb);
        ImGui::Text("Actual active ratio: %.2f", (float)ctx.render_data.max_active_count / (float)MAX_ACTIVE_COUNT);
        ImGui::Text("Actual tmp ratio: %.2f", (float)ctx.render_data.max_tmp_count / (float)MAX_TMP_COUNT);




        ImGui::SeparatorText("Render");
        ImGui::Combo("Shading mode", &ctx.render_data.shading_mode, "Shaded\0Heatmap\0Normals\0AO\0");
        if (ctx.render_data.shading_mode == SHADING_MODE_HEATMAP) {
            ImGui::SliderInt("Colormap max", &ctx.render_data.colormap_max, 1, 64);
        }
        ImGui::SliderInt("Samples per pixel", &ctx.render_data.num_samples, 1, 64);

        ImGui::SliderFloat("Gamma", &ctx.render_data.gamma, 1, 4);

#if 0
        if (ImGui::Button("Copy command")) {
            char command[1024];
            snprintf(command, 1024, "-i %s --cam_yaw=%f --cam_pitch=%f --cam_dist=%f --target_x=%f --target_y=%f --target_z=%f --min_x=%f --min_y=%f --min_z=%f --max_x=%f --max_y=%f --max_z=%f --gamma=%f",
                input_file.c_str(),
                cam_yaw,
                cam_pitch,
                cam_distance,
                cam_target.x,
                cam_target.y,
                cam_target.z,
                ctx.render_data.aabb_min.x,
                ctx.render_data.aabb_min.y,
                ctx.render_data.aabb_min.z,
                ctx.render_data.aabb_max.x,
                ctx.render_data.aabb_max.y,
                ctx.render_data.aabb_max.z,
                ctx.render_data.gamma
            );
            glfwSetClipboardString(ctx.init.window, command);
        }
#endif


         timing = ctx.render(cam_position, cam_target);
    }
    VK_CHECK(ctx.init.disp.deviceWaitIdle());

    //{
    //    FILE* fp = fopen("timings.csv", "w");
    //    for (int i = 0; i < 10000; i++) {
    //        fprintf(fp, "%f\n", g_timings[i]);
    //    }
    //    fflush(fp);
    //    fclose(fp);
    //}

    return 0;
}
