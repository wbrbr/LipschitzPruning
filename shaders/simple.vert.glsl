#version 450
#extension GL_ARB_separate_shader_objects : enable


vec2 positions[3] = vec2[](vec2 (-1, 1), vec2 (3, -1), vec2 (-1, 3));

void main ()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f + -1.0f, 0.0f, 1.0f);
}