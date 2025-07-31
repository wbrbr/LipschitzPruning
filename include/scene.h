#ifndef SDFCULLING_SCENE_H
#define SDFCULLING_SCENE_H
#include "utils.h"

void load_json(const char* path, std::vector<CSGNode>& nodes, glm::vec3& aabb_min, glm::vec3& aabb_max);
void write_json(const std::vector<CSGNode>& nodes, const char* path);
#endif //SDFCULLING_SCENE_H
