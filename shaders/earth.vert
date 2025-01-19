#version 330 core

uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
// layout (location = 1) in vec3 in_texcoord;

out vec3 position;
out vec2 texcoord;

#define PI 3.1415926535897932384626433832795

// Get latitude and longitude (in radians) for a point on unit sphere
vec2 point_to_geo_coords(vec3 point) {
    float lat = asin(point.y);
    float lng = atan(point.x, point.z);
    return vec2(lat, lng);
}

vec2 geo_coords_to_tex_coords(vec2 geo_coords) {
    return vec2((geo_coords.y + PI) / (2 * PI),
                (-geo_coords.x + PI / 2) / PI);
}

struct Geodata {
    float height_multiplier;
    float earth_radius_at_peak;
    float earth_radius_at_sea;
};
uniform sampler2D heightmap;
uniform Geodata geodata;

void main()
{
    vec2 geo_coords = point_to_geo_coords(in_position);
    texcoord = geo_coords_to_tex_coords(geo_coords);

    float sea_radius = geodata.earth_radius_at_sea / geodata.earth_radius_at_peak;
    
    float height = texture(heightmap, texcoord).x;
    float radius = sea_radius + geodata.height_multiplier * height * (1 - sea_radius);

    gl_Position = projection * view * vec4(radius * in_position, 1);
    position = vec4(in_position, 1).xyz;

}
