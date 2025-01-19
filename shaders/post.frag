#version 330 core

in vec2 texcoord;

// task 3 - ..
uniform sampler2D hdr_buffer;

layout (location = 0) out vec4 out_color;

void main() {
    const float gamma = 2.2;

    vec3 color = texture(hdr_buffer, texcoord).rgb;

    // https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    color = (color * (a * color + b)) / (color * (c * color + d) + e);

    // gamma correction
    color = pow(color, vec3(1.0 / gamma));

    out_color = vec4(color, 1);
}
