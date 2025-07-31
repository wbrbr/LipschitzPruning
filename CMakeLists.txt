cmake_minimum_required(VERSION 3.18)
project(LipschitzPruning LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(ext/vk-bootstrap)

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(

        CLI11

        GIT_REPOSITORY "https://github.com/CLIUtils/CLI11"

        GIT_TAG "v2.5.0"

)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY "https://github.com/glfw/glfw"
    GIT_TAG 3.4
)

FetchContent_MakeAvailable(CLI11)
FetchContent_MakeAvailable(glfw)

set(SHADER_SRCS
        simple.vert.glsl
        simple.frag.glsl
        culling.comp.glsl
        plane.vert.glsl
        plane.frag.glsl
        dense_eval.comp.glsl)
set(SHADER_STAGES
        vert
        frag
        comp
        vert
        frag
        comp)
set(SHADER_BINS
        vert.spv
        frag.spv
        culling.comp.spv
        plane.vert.spv
        plane.frag.spv
        dense_eval.comp.spv)

set(SHARED_SRC
        src/utils.cpp
        src/debug_plane.cpp
        src/context.cpp
        src/scene.cpp
        ext/imgui/imgui.cpp
        ext/imgui/imgui_draw.cpp
        ext/imgui/imgui_demo.cpp
        ext/imgui/imgui_tables.cpp
        ext/imgui/imgui_widgets.cpp
        ext/imgui/backends/imgui_impl_vulkan.cpp
        ext/imgui/backends/imgui_impl_glfw.cpp
)

foreach(src_file bin_file stage IN ZIP_LISTS SHADER_SRCS SHADER_BINS SHADER_STAGES)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/${bin_file}" COMMAND glslc ARGS -fshader-stage=${stage} --target-spv=spv1.3 -g ${CMAKE_SOURCE_DIR}/shaders/${src_file} -o ${CMAKE_BINARY_DIR}/${bin_file} MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/shaders/${src_file} DEPENDS ${CMAKE_SOURCE_DIR}/shaders/common.glsl ${CMAKE_SOURCE_DIR}/shaders/common_culling.glsl ${CMAKE_SOURCE_DIR}/include/constants.h)
endforeach()

include_directories(PRIVATE include/ ext/imgui ext/json/include ext/rapidjson/include)
link_libraries(glfw vk-bootstrap Vulkan::Vulkan CLI11::CLI11)

add_executable(LipschitzPruning src/main.cpp ${SHARED_SRC})
foreach(bin_file IN LISTS SHADER_BINS)
    target_sources(LipschitzPruning PRIVATE ${CMAKE_BINARY_DIR}/${bin_file})
endforeach()
