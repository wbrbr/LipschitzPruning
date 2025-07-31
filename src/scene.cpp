#include "scene.h"
#include <queue>
#include <random>
#include <rapidjson/document.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/writer.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#include "glm/gtx/transform.hpp"

void load_json(const char *path, std::vector<CSGNode> &nodes, glm::vec3& aabb_min, glm::vec3& aabb_max) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        abort();
    }
    fseek(fp, 0, SEEK_END);
    size_t s = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::vector<char> buf(s + 1);
    if (fread(buf.data(), 1, s, fp) != s) {
        fprintf(stderr, "Failed to read file: %s\n", path);
        abort();
    }
    fclose(fp);
    buf[s] = 0;
    rapidjson::Document d;
    d.Parse(buf.data());
    if (d.HasParseError()) {
        rapidjson::ParseErrorCode code = d.GetParseError();
        fprintf(stderr, "JSON parsing failed: error %d\n", code);
        abort();
    }

    auto aabb_min_arr = d["aabb_min"].GetArray();
    aabb_min.x = aabb_min_arr[0].GetFloat();
    aabb_min.y = aabb_min_arr[1].GetFloat();
    aabb_min.z = aabb_min_arr[2].GetFloat();
    auto aabb_max_arr = d["aabb_max"].GetArray();
    aabb_max.x = aabb_max_arr[0].GetFloat();
    aabb_max.y = aabb_max_arr[1].GetFloat();
    aabb_max.z = aabb_max_arr[2].GetFloat();

    struct StackEntry {
        const rapidjson::Value* j;
        glm::mat4 mat;
        int idx;
        bool sign;
    };

    nodes.push_back({});
    std::vector<StackEntry> stack = { { &d, glm::mat4(1), 0, true } };

    while (!stack.empty()) {
        StackEntry e = stack.back();
        stack.pop_back();

        const auto& j = *e.j;

        auto m = j["matrix"].GetArray();
        glm::mat4 node_mat = glm::mat4(
            m[0].GetFloat(), m[4].GetFloat(), m[8].GetFloat(), 0.f,
            m[1].GetFloat(), m[5].GetFloat(), m[9].GetFloat(), 0.f,
            m[2].GetFloat(), m[6].GetFloat(), m[10].GetFloat(), 0.f,
            m[3].GetFloat(), m[7].GetFloat(), m[11].GetFloat(), 1.f
        );
        glm::mat4 mat = e.mat;
        std::string type = j["nodeType"].GetString();
        int node_idx = e.idx;
        if (type == "primitive") {
            CSGNode &node = nodes[node_idx];
            node.type = NODETYPE_PRIMITIVE;
            node.left = -1;
            node.right = -1;
            node.sign = e.sign;

            glm::mat4 world_to_prim = mat * node_mat;
            node.primitive.m_row0 = glm::vec4(world_to_prim[0][0], world_to_prim[1][0], world_to_prim[2][0],
                                              world_to_prim[3][0]);
            node.primitive.m_row1 = glm::vec4(world_to_prim[0][1], world_to_prim[1][1], world_to_prim[2][1],
                                              world_to_prim[3][1]);
            node.primitive.m_row2 = glm::vec4(world_to_prim[0][2], world_to_prim[1][2], world_to_prim[2][2],
                                              world_to_prim[3][2]);

            node.primitive.extrude_rounding.x = j["round_x"].GetFloat();
            node.primitive.extrude_rounding.y = j["round_y"].GetFloat();

            {
                auto c = j["color"].GetArray();
                node.primitive.color = 0;
                node.primitive.color |= std::min((uint32_t)(c[0].GetFloat() * 255.99f), 255u) << 0;
                node.primitive.color |= std::min((uint32_t)(c[1].GetFloat() * 255.99f), 255u) << 8;
                node.primitive.color |= std::min((uint32_t)(c[2].GetFloat() * 255.99f), 255u) << 16;
            }

            std::string prim_type = j["primitiveType"].GetString();
            if (prim_type == "sphere") {
                node.primitive.type = PRIMITIVE_SPHERE;
            } else if (prim_type == "box") {
                node.primitive.type = PRIMITIVE_BOX;
            } else if (prim_type == "cylinder") {
                node.primitive.type = PRIMITIVE_CYLINDER;
            } else if (prim_type == "cone") {
                node.primitive.type = PRIMITIVE_CONE;
            } else {
                fprintf(stderr, "Unknown primitive: %s\n", prim_type.c_str());
                abort();
            }

            switch (node.primitive.type) {
                case PRIMITIVE_SPHERE: {
                    float r = j["radius"].GetFloat();
                    node.primitive.sphere.radius = glm::vec4(r, 0, 0, 0);
                    break;
                }
                case PRIMITIVE_BOX: {
                    auto s = j["sides"].GetArray();
                    float s_x = s[0].GetFloat();
                    float s_y = s[1].GetFloat();
                    float s_z = s[2].GetFloat();
                    node.primitive.box.sizes = glm::vec4(s_x, s_y, s_z, 0);

                    float scale = fmaxf(node.primitive.box.sizes.x, node.primitive.box.sizes.z) * 2;
                    auto c = j["bevel"].GetArray();
                    glm::vec4 corner_rounding;
                    corner_rounding.x = c[0].GetFloat();
                    corner_rounding.y = c[1].GetFloat();
                    corner_rounding.z = c[2].GetFloat();
                    corner_rounding.w = c[3].GetFloat();

    #if 1
                    uint32_t corner_data = 0;
                    corner_data |= (uint32_t)(corner_rounding.x * 255.f * 2.f / scale) << 0;
                    corner_data |= (uint32_t)(corner_rounding.y * 255.f * 2.f / scale) << 8;
                    corner_data |= (uint32_t)(corner_rounding.z * 255.f * 2.f / scale) << 16;
                    corner_data |= (uint32_t)(corner_rounding.w * 255.f * 2.f / scale) << 24;
                    memcpy(&node.primitive.box.sizes.w, &corner_data, sizeof(uint32_t));
    #endif
                    break;
                }
                case PRIMITIVE_CYLINDER: {
                    float h = j["height"].GetFloat();
                    float r = j["radius"].GetFloat();
                    node.primitive.cylinder.height = h;
                    node.primitive.cylinder.radius = r;
                    break;
                }
                case PRIMITIVE_CONE: {
                    float h = j["height"].GetFloat();
                    float r = j["radius"].GetFloat();
                    node.primitive.cone.height = h;
                    node.primitive.cone.radius = r;
                    break;
                }

                default:
                    abort();
            }
        } else if (type == "binaryOperator") {
            std::string blend_mode = j["blendMode"].GetString();
            uint32_t op;
            if (blend_mode == "union") {
                op = OP_UNION;
            } else if (blend_mode == "sub") {
                op = OP_SUB;
            } else if (blend_mode == "inter") {
                op = OP_INTER;
            } else {
                fprintf(stderr, "Unknown blend mode: %s\n", blend_mode.c_str());
                abort();
            }

            int left_idx = nodes.size();
            nodes.push_back({});
            int right_idx = nodes.size();
            nodes.push_back({});

            stack.push_back({ &j["leftChild"], mat * node_mat, left_idx, 1 });
            stack.push_back({ &j["rightChild"], mat * node_mat, right_idx, op != OP_SUB });

            CSGNode &node = nodes[node_idx];
            node.left = left_idx;
            node.right = right_idx;
            node.sign = e.sign;
            float k = j["blendRadius"].GetFloat();

            node.binary_op = BinaryOp(k, op == OP_UNION, op);
        } else {
            fprintf(stderr, "invalid type: %s\n", type.c_str());
            abort();
        }
    }
}

rapidjson::Value write_node(rapidjson::Document &d, const std::vector<CSGNode> &nodes, int node_idx) {
    CSGNode node = nodes[node_idx];

    rapidjson::Value v(rapidjson::kObjectType);

    auto a = d.GetAllocator();


    glm::mat4x3 mat;
    switch (node.type) {
        case NODETYPE_BINARY: {
            v.AddMember("nodeType", "binaryOperator", a);
            mat = glm::mat3x4(1);
            rapidjson::Value left_child = write_node(d, nodes, node.left);
            rapidjson::Value right_child = write_node(d, nodes, node.right);
            v.AddMember("leftChild", left_child, a);
            v.AddMember("rightChild", right_child, a);
            float k;
            uint32_t k_uint = node.binary_op.blend_factor_and_sign & ~7u;
            memcpy(&k, &k_uint, sizeof(float));
            v.AddMember("blendRadius", k, a);
            uint32_t op = (node.binary_op.blend_factor_and_sign >> 1) & 3u;
            switch (op) {
                case OP_UNION:
                    v.AddMember("blendMode", "union", a);
                    break;
                case OP_INTER:
                    v.AddMember("blendMode", "inter", a);
                    break;
                case OP_SUB:
                    v.AddMember("blendMode", "sub", a);
                    break;
                default:
                    abort();
            }
            break;
        }

        case NODETYPE_PRIMITIVE: {
            Primitive p = node.primitive;
            v.AddMember("nodeType", "primitive", a);
            mat = glm::transpose(glm::mat3x4(p.m_row0, p.m_row1, p.m_row2));

            {
                float r = (float)((node.primitive.color >> 0) & 0xff) / 255.99f;;
                float g = (float)((node.primitive.color >> 8) & 0xff) / 255.99f;;
                float b = (float)((node.primitive.color >> 16) & 0xff) / 255.99f;;
                rapidjson::Value color_arr(rapidjson::kArrayType);
                color_arr.PushBack(r, a);
                color_arr.PushBack(g, a);
                color_arr.PushBack(b, a);
                v.AddMember("color", color_arr, a);
            }

            v.AddMember("round_x", node.primitive.extrude_rounding.x, a);
            v.AddMember("round_y", node.primitive.extrude_rounding.y, a);
            switch (node.primitive.type) {
                case PRIMITIVE_BOX: {
                    v.AddMember("primitiveType", "box", a);
                    rapidjson::Value sides_arr(rapidjson::kArrayType);
                    sides_arr.PushBack(rapidjson::Value(node.primitive.box.sizes.x).Move(), a);
                    sides_arr.PushBack(rapidjson::Value(node.primitive.box.sizes.y).Move(), a);
                    sides_arr.PushBack(rapidjson::Value(node.primitive.box.sizes.z).Move(), a);
                    v.AddMember("sides", sides_arr, a);

                    float scale = fmaxf(node.primitive.box.sizes.x, node.primitive.box.sizes.z) * 2;
                    rapidjson::Value bevel_arr(rapidjson::kArrayType);
                    uint32_t corner_data;
                    memcpy(&corner_data, &node.primitive.box.sizes.w, sizeof(uint32_t));

                    glm::vec4 corner_rounding;
                    corner_rounding.x = (float)((corner_data >> 0) & 0xff);
                    corner_rounding.y = (float)((corner_data >> 8) & 0xff);
                    corner_rounding.z = (float)((corner_data >> 16) & 0xff);
                    corner_rounding.w = (float)((corner_data >> 24) & 0xff);
                    corner_rounding = corner_rounding * scale / 255.f;
                    corner_rounding /= 2;
                    bevel_arr.PushBack(corner_rounding.x, a);
                    bevel_arr.PushBack(corner_rounding.y, a);
                    bevel_arr.PushBack(corner_rounding.z, a);
                    bevel_arr.PushBack(corner_rounding.w, a);
                    v.AddMember("bevel", bevel_arr, a);
                }
                break;

                case PRIMITIVE_CONE:
                    v.AddMember("primitiveType", "cone", a);
                    v.AddMember("height", node.primitive.cone.height, a);
                    v.AddMember("radius", node.primitive.cone.radius, a);
                    break;

                case PRIMITIVE_SPHERE:
                    v.AddMember("primitiveType", "sphere", a);
                    v.AddMember("radius", node.primitive.sphere.radius.x, a);
                    break;

                default:
                    abort();
            }
            break;
        }
    }
    {
        rapidjson::Value mat_array(rapidjson::kArrayType);
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 4; col++) {
                rapidjson::Value f(rapidjson::kNumberType);
                f.SetFloat(mat[col][row]);
                mat_array.PushBack(f, a);
            }
        }
        v.AddMember("matrix", mat_array, a);
    }

    return v;
}

void write_json(const std::vector<CSGNode> &nodes, const char *path) {
    rapidjson::Document d;
    d.SetObject();

    rapidjson::Value v = write_node(d, nodes, nodes.size() - 1);
    for (auto &m: v.GetObject()) {
        d.AddMember(m.name, m.value, d.GetAllocator());
    }

    FILE *fp = fopen(path, "w");

    char write_buf[64 * 1024];
    rapidjson::FileWriteStream os(fp, write_buf, sizeof(write_buf));
    rapidjson::Writer<rapidjson::FileWriteStream> writer(os);
    d.Accept(writer);

    fflush(fp);
    fclose(fp);
}