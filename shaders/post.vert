#version 330 core

out vec2 texcoord;

vec2 vertices[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main() {
    vec2 vertex = vertices[gl_VertexID];
    gl_Position = vec4(vertex, 0.0, 1.0);
    texcoord = vertex * 0.5 + vec2(0.5);
}
