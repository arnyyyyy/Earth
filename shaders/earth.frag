#version 330 core

struct Material {
    sampler2D diffuse_day_texture;
    sampler2D diffuse_night_texture;
    sampler2D specular_texture;
};

struct Geodata {
    float height_multiplier;
    float earth_radius_at_peak;
    float earth_radius_at_sea;
};

struct AmbientLight {
    vec3 color;  
};

struct Sun {
    vec3 pos;
    vec3 color;
};

in vec3 position;
in vec2 texcoord;

uniform vec3 camera_position;

uniform Material material;
uniform sampler2D heightmap;
uniform Geodata geodata;
uniform AmbientLight ambient_light;
uniform Sun sun;

layout (location = 0) out vec4 out_color;

#define PI 3.1415926535897932384626433832795

vec2 tex_coords_to_geo_coords(vec2 tex_coords) {
    return vec2(-tex_coords.y * PI + PI / 2, // latitude
                tex_coords.x * 2 * PI - PI); // longiture
}

vec3 geo_coords_to_world_point(vec2 geo_coords) {
    float y = sin(geo_coords.x);
    float r = cos(geo_coords.x);
    float x = sin(geo_coords.y) * r;
    float z = cos(geo_coords.y) * r;
    return vec3(x, y, z);
}

vec3 texcoord_to_world_point(vec2 tex_coords) {    
    float sea_radius = geodata.earth_radius_at_sea / geodata.earth_radius_at_peak;

    vec2 geo_coords = tex_coords_to_geo_coords(tex_coords);
    vec3 point = geo_coords_to_world_point(geo_coords);
    
    float height = texture(heightmap, tex_coords).x;
    float radius = sea_radius + geodata.height_multiplier * height * (1 - sea_radius);
    return radius * point;
}

void main()
{
    // Calc the normal vector

    vec2 texel_size = 1.0 / vec2(textureSize(heightmap, 0));

    vec3 p_west = texcoord_to_world_point(texcoord - vec2(texel_size.x, 0));
    vec3 p_east = texcoord_to_world_point(texcoord + vec2(texel_size.x, 0));
    vec3 p_south = texcoord_to_world_point(texcoord + vec2(0, texel_size.y));
    vec3 p_north = texcoord_to_world_point(texcoord - vec2(0, texel_size.y));

    vec3 d_north = normalize(p_north - p_south);
    vec3 d_east = normalize(p_east - p_west);
    vec3 norm = normalize(cross(d_north, d_east));


    // Calc light

    vec3 view_dir = normalize(camera_position - position);
    vec3 sunlight_dir = normalize(sun.pos);

    float diffuse = max(0.0, dot(norm, sunlight_dir));;

    vec3 reflected_dir = reflect(sunlight_dir, norm);
    float glossiness = texture(material.specular_texture, texcoord).x;
    float specular_power = 5.f;
    float specular = glossiness * pow(max(0.0, dot(reflected_dir, view_dir)), specular_power);

    vec3 light = sun.color * (diffuse + specular); 

    vec3 albedo_day = texture(material.diffuse_day_texture, texcoord).xyz;
    vec3 albedo_night = texture(material.diffuse_night_texture, texcoord).xyz;

    vec3 color = max(vec3(0), 1 - light) * albedo_night + light * albedo_day;
    out_color = vec4(color, 1);
}
