#include <filesystem>
#include <fstream>
#include <string_view>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <vector>
#include <map>
#include <cmath>
#include <cassert>
#include <cstring>
#include <algorithm>

#include "SDL2/SDL.h"
#include "SDL2/SDL_mouse.h"
#include "SDL2/SDL_video.h"

#include "GL/glew.h"

#define GLM_FORCE_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/vec3.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/compatibility.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/common.hpp"

#include "stb_image.h"



GLuint load_texture(const std::filesystem::path &path, bool srgb = false);
std::string read_file(const std::filesystem::path &path);

GLuint create_shader(GLenum type, const char *source);
GLuint create_program(GLuint vertex_shader, GLuint fragment_shader);

void generate_sphere(std::vector<glm::vec3> &vertices, size_t subdivisions_num);


std::string to_string(std::string_view str) {
    return std::string(str.begin(), str.end());
}

void sdl2_fail(std::string_view message) {
    throw std::runtime_error(to_string(message) + SDL_GetError());
}

void glew_fail(std::string_view message, GLenum error) {
    throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(glewGetErrorString(error)));
}

int main() try {
    std::filesystem::path project_root = PROJECT_ROOT;
    
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        sdl2_fail("SDL_Init: ");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow("HW4",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          800, 600,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    if (!window)
        sdl2_fail("SDL_CreateWindow: ");


    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        sdl2_fail("SDL_GL_CreateContext: ");

    if (auto result = glewInit(); result != GLEW_NO_ERROR)
        glew_fail("glewInit: ", result);

    if (!GLEW_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 is not supported");


    // Load and compile shaders

    auto load_shaders = [&](const char *name) -> GLuint {
        auto vertex_shader_source = read_file((project_root / ((std::string) "shaders/" + name + ".vert")).c_str());
        auto fragment_shader_source = read_file((project_root / ((std::string) "shaders/" + name + ".frag")).c_str());

        auto vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_shader_source.c_str());
        auto fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_shader_source.c_str());

        return create_program(vertex_shader, fragment_shader);
    };
    GLuint earth_program = load_shaders("earth");
    GLuint post_program = load_shaders("post");


    // Load textures

    GLuint earth_diffuse_day_texture = load_texture(project_root / "earth_diffuse_day.jpg", true);
    GLuint earth_diffuse_night_texture = load_texture(project_root / "earth_diffuse_night.jpg", true);
    GLuint earth_specular_texture = load_texture(project_root / "earth_specular.jpg", false);
    GLuint earth_heightmap_texture = load_texture(project_root / "earth_heightmap.png", false);


    // Get uniform's locations

    struct {
        struct {
            GLint view; // mat4
            GLint projection; // mat4

            GLint camera_position; // vec3

            struct {
                GLint diffuse_day_texture; // sampler2D
                GLint diffuse_night_texture; // sampler2D
                GLint specular_texture; // sampler2D
            } material;

            struct {
                GLint height_multiplier; // float
                GLint earth_radius_at_peak; // float
                GLint earth_radius_at_sea; // float
            } geodata;

            GLint heightmap; // sampler2D

            struct {
                GLint color; // vec3
            } ambient_light;

            struct {
                GLint pos; // vec3
                GLint color; // vec3
            } sun;
        } earth;

        struct {
            GLint hdr_buffer; // sampler2D
        } post;

    } locations;

    locations.earth.view = glGetUniformLocation(earth_program, "view");
    locations.earth.projection = glGetUniformLocation(earth_program, "projection");
    locations.earth.camera_position = glGetUniformLocation(earth_program, "camera_position");

    locations.earth.material.diffuse_day_texture = glGetUniformLocation(earth_program, "material.diffuse_day_texture");
    locations.earth.material.diffuse_night_texture = glGetUniformLocation(earth_program, "material.diffuse_night_texture");
    locations.earth.material.specular_texture = glGetUniformLocation(earth_program, "material.specular_texture");

    locations.earth.heightmap = glGetUniformLocation(earth_program, "heightmap");
    locations.earth.geodata.earth_radius_at_peak = glGetUniformLocation(earth_program, "geodata.earth_radius_at_peak");
    locations.earth.geodata.earth_radius_at_sea = glGetUniformLocation(earth_program, "geodata.earth_radius_at_sea");
    locations.earth.geodata.height_multiplier = glGetUniformLocation(earth_program, "geodata.height_multiplier");

    locations.earth.sun.pos = glGetUniformLocation(earth_program, "sun.pos");
    locations.earth.sun.color = glGetUniformLocation(earth_program, "sun.color");

    locations.earth.ambient_light.color = glGetUniformLocation(earth_program, "ambient_light.color");

    locations.post.hdr_buffer = glGetUniformLocation(post_program, "hdr_buffer");


    // Create buffers for the scene and generate data

    size_t earth_vertices_count;

    GLuint earth_vao, earth_vbo;
    glGenVertexArrays(1, &earth_vao);
    glBindVertexArray(earth_vao);

    glGenBuffers(1, &earth_vbo);

    glBindBuffer(GL_ARRAY_BUFFER, earth_vbo);
    {
        const size_t SUBDIVISIONS_NUM = 8;
        std::vector<glm::vec3> earth_vertices;
        generate_sphere(earth_vertices, SUBDIVISIONS_NUM);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * earth_vertices.size(), earth_vertices.data(), GL_STATIC_DRAW);
        earth_vertices_count = earth_vertices.size();
    } // scope the vector to deallocate it early

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void *) 0);


    GLuint post_vao;
    glGenVertexArrays(1, &post_vao);


    // Gen a floating-point frame buffer for HDR rendering

    GLuint hdr_buffer;
    glGenTextures(1, &hdr_buffer);
    glBindTexture(GL_TEXTURE_2D, hdr_buffer);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint hdr_rbo;
    glGenRenderbuffers(1, &hdr_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, hdr_rbo);

    GLuint hdr_fbo;
    glGenFramebuffers(1, &hdr_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr_fbo);
    glFramebufferTexture(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, hdr_buffer, 0);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdr_rbo);

    auto resize_hdr_buffers = [&]() {    
        glBindTexture(GL_TEXTURE_2D, hdr_buffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, hdr_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr_fbo);
        assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    };

    resize_hdr_buffers();


    // Dynamic state

    auto last_frame_start = std::chrono::high_resolution_clock::now();

    float time = 0.f;
    bool paused = false;

    float view_angle = glm::pi<float>() / 12.f;
    float camera_distance = 2.5f;
    float camera_rotation = 0.f;

    std::map<SDL_Keycode, bool> button_down;

    bool running = true;
    while (running) {
        for (SDL_Event event; SDL_PollEvent(&event);)
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_RESIZED:
                            width = event.window.data1;
                            height = event.window.data2;
                            resize_hdr_buffers();
                            break;
                    }
                    break;
                case SDL_KEYDOWN:
                    button_down[event.key.keysym.sym] = true;
                    switch (event.key.keysym.sym) {
                        case SDLK_SPACE:
                            paused = !paused;
                            break;                            
                    }
                    break;
                case SDL_KEYUP:
                    button_down[event.key.keysym.sym] = false;
                    break;
            }

        if (!running)
            break;

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration_cast<std::chrono::duration<float>>(now - last_frame_start).count();
        last_frame_start = now;
        if (!paused)
            time += dt;


        float camera_speed = std::min(camera_distance - 1, 3.f);


        if (button_down[SDLK_UP])
            camera_distance -= std::min(camera_speed, 3.f) * dt;
        if (button_down[SDLK_DOWN])
            camera_distance += std::min(camera_speed, 3.f)  * dt;

        if (button_down[SDLK_a])
            camera_rotation += std::min(camera_speed, 2.f) * dt;
        if (button_down[SDLK_d])
            camera_rotation -= std::min(camera_speed, 2.f) * dt;

        if (button_down[SDLK_w])
            view_angle += std::min(camera_speed, 2.f) * dt;
        if (button_down[SDLK_s])
            view_angle -= std::min(camera_speed, 2.f) * dt;


        // Calc matrices for the scene
        const float near = 0.001f;
        const float far = 20.f;
        glm::mat4 camera_projection_mat = glm::perspective(glm::pi<float>() / 2.f, (1.f * width) / height, near, far);

        glm::mat4 camera_view_mat(1.f);
        camera_view_mat = glm::translate(camera_view_mat, {0.f, 0.f, -camera_distance});
        camera_view_mat = glm::rotate(camera_view_mat, view_angle, {1.f, 0.f, 0.f});
        camera_view_mat = glm::rotate(camera_view_mat, camera_rotation, {0.f, 1.f, 0.f});

        glm::vec3 camera_pos = (glm::inverse(camera_view_mat) * glm::vec4(0.f, 0.f, 0.f, 1.f)).xyz();
        
        const float height_multiplier = 10.f;
        const float earth_radius_at_peak_km = 6400.f;
        const float earth_radius_at_sea_km = 6378.137f;

        float sun_angle = std::fmod(time, 2 * M_PI);
        glm::vec3 sun_pos(std::cos(sun_angle), 0.f, std::sin(sun_angle));


        // Render the earth into the HDR buffer

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr_fbo);
        glViewport(0, 0, width, height);

        glClearColor(0.0f, 0.0f, 0.0f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glBindVertexArray(earth_vao);

        glUseProgram(earth_program);

        glUniformMatrix4fv(locations.earth.view, 1, GL_FALSE, glm::value_ptr(camera_view_mat));
        glUniformMatrix4fv(locations.earth.projection, 1, GL_FALSE, glm::value_ptr(camera_projection_mat));
        glUniform3fv(locations.earth.camera_position, 1, glm::value_ptr(camera_pos));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, earth_diffuse_day_texture);
        glUniform1i(locations.earth.material.diffuse_day_texture, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, earth_diffuse_night_texture);
        glUniform1i(locations.earth.material.diffuse_night_texture, 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, earth_specular_texture);
        glUniform1i(locations.earth.material.specular_texture, 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, earth_heightmap_texture);
        glUniform1i(locations.earth.heightmap, 3);

        glUniform1f(locations.earth.geodata.earth_radius_at_peak, earth_radius_at_peak_km);
        glUniform1f(locations.earth.geodata.earth_radius_at_sea, earth_radius_at_sea_km);
        glUniform1f(locations.earth.geodata.height_multiplier, height_multiplier);

        glUniform3fv(locations.earth.sun.pos, 1, glm::value_ptr(sun_pos));
        glUniform3f(locations.earth.sun.color, 2.f, 2.f, 2.f);

        glUniform3f(locations.earth.ambient_light.color, 0.5f, 0.5f, 0.5f);

        glDrawArrays(GL_TRIANGLES, 0, earth_vertices_count);


        // Render the HDR buffer with post processing
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glUseProgram(post_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr_buffer);
        glUniform1i(locations.post.hdr_buffer, 0);

        glBindVertexArray(post_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);



        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
}
catch (std::exception const &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error((std::string) "failed to read the file at " + (std::string) path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


const char *gl_error_str(GLenum error);

GLuint load_texture(const std::filesystem::path &path, bool srgb) {
    int width, height, channels;
    void* data; 
    data = stbi_load(path.c_str(), &width, &height, &channels, 4); // RGBA
    if (!data) {
        throw std::runtime_error((std::string) "Failed to load texture: " + (std::string) path);
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    GLenum internal_format = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        stbi_image_free(data);
        throw std::runtime_error((std::string) "OpenGL error during texture upload: " + gl_error_str(error));
    }
    
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);

    return textureID;
}


GLuint create_shader(GLenum type, const char *source) {
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint status;
    glGetShaderiv(result, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint info_log_length;
        glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Shader compilation failed: " + info_log);
    }
    return result;
}


GLuint create_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint result = glCreateProgram();
    glAttachShader(result, vertex_shader);
    glAttachShader(result, fragment_shader);
    glLinkProgram(result);

    GLint status;
    glGetProgramiv(result, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint info_log_length;
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
        std::string info_log(info_log_length, '\0');
        glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
        throw std::runtime_error("Program linkage failed: " + info_log);
    }

    return result;
}

void generate_sphere(std::vector<glm::vec3> &vertices,
                     size_t subdivisions_num) {
    // Start with a regular icosahedron
    // Taken from https://github.com/lazysquirrellabs/sphere_generator/blob/361e4e64cc1b3ecd00db495181b4ec8adabcf37c/Assets/Libraries/SphereGenerator/Runtime/Generators/IcosphereGenerator.cs#L35
    vertices = {
        {0.8506508f,           0.5257311f,         0.f},           // 0
        {0.000000101405476f,   0.8506507f,        -0.525731f},     // 1
        {0.000000101405476f,   0.8506506f,         0.525731f},     // 2
        {0.5257309f,          -0.00000006267203f, -0.85065067f},   // 3
        {0.52573115f,         -0.00000006267203f,  0.85065067f},   // 4
        {0.8506508f,          -0.5257311f,         0.f},           // 5
        {-0.52573115f,         0.00000006267203f, -0.85065067f},   // 6
        {-0.8506508f,          0.5257311f,         0.f},           // 7
        {-0.5257309f,          0.00000006267203f,  0.85065067f},   // 8
        {-0.000000101405476f, -0.8506506f,        -0.525731f},     // 9
        {-0.000000101405476f, -0.8506507f,         0.525731f},     // 10
        {-0.8506508f,         -0.5257311f,         0.f}            // 11
	};
	std::vector<uint32_t> indices = {
         0,  1,  2,
         0,  3,  1,
         0,  2,  4,
         3,  0,  5,
         0,  4,  5,
         1,  3,  6,
         1,  7,  2,
         7,  1,  6,
         4,  2,  8,
         7,  8,  2,
         9,  3,  5,
         6,  3,  9,
         5,  4, 10,
         4,  8, 10,
         9,  5, 10,
         7,  6, 11,
         7, 11,  8,
        11,  6,  9,
         8, 11, 10,
        10, 11,  9
    };


    // Prepare the data for subdivision
    for (auto &v : vertices)
        v = glm::normalize(v);

    using Face = std::array<glm::vec3, 3>;

    std::vector<Face> faces;
    for (size_t i = 0; i + 3 <= indices.size(); i += 3) {
        faces.push_back({
            vertices[indices[i + 0]],
            vertices[indices[i + 1]],
            vertices[indices[i + 2]],
        });
    }
    

    // On each iteration, subdivide each face into 4
    for (size_t iter = 0; iter < subdivisions_num; ++iter) {
        std::vector<Face> next_faces;
        next_faces.reserve(faces.size() * 4);
        for (auto[v0, v1, v2] : faces) {
            //        v2
            //      /   \
            //     v5---v4
            //    /  \ / \
            //   v0--v3--v1
            glm::vec3 v3 = glm::normalize((v0 + v1) / 2.f);
            glm::vec3 v4 = glm::normalize((v1 + v2) / 2.f);
            glm::vec3 v5 = glm::normalize((v0 + v2) / 2.f);

            next_faces.push_back({v0, v3, v5});
            next_faces.push_back({v3, v1, v4});
            next_faces.push_back({v5, v4, v2});
            next_faces.push_back({v3, v4, v5});
        }

        faces.swap(next_faces);
    }


    vertices.clear();
    vertices.reserve(faces.size() * 3);

    for (size_t i = 0; i < faces.size(); ++i) {
        auto[v0, v1, v2] = faces[i];
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
    }
}

const char *gl_error_str(GLenum error) {
    switch (error) {
        case GL_NO_ERROR:
            return "No error";
        case GL_INVALID_ENUM:
            return "Invalid enum";
        case GL_INVALID_VALUE:
            return "Invalid value";
        case GL_INVALID_OPERATION:
            return "Invalid operation";
        case GL_STACK_OVERFLOW:
            return "Stack overflow";
        case GL_STACK_UNDERFLOW:
            return "Stack underflow";
        case GL_OUT_OF_MEMORY:
            return "Out of memory";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "Invalid framebuffer operation";
        default:
            return "Unknown error";
    }
}
