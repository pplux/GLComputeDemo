#include <cstdlib>
#include <iostream>
#include "imgui.h" // > imgui headers (+imgui_app definitions)
#include "sokol.h" // > for advanced sokol handling (advanced)
#include "glad/glad.h"

struct ComputeState {
    GLuint texture;
    const int width = 512;
    const int height = 512;
};

struct CustomRender {

    struct {
		GLuint program;
		GLuint vao;
    } render;

    struct {
        GLuint texture;
        GLuint program;
    } compute;

    struct {
        GLuint program;
        GLuint vao;
    } save;

    void init() {

        auto checkShader = [](GLuint shader) {
            GLint result = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
            if (result == GL_FALSE) {
                char buffer[1024];
                glGetShaderInfoLog(shader, sizeof(buffer), nullptr, buffer);
                printf("ERROR Shader Compilation failed\n%s", buffer);
                exit(-1);
            }
        };

        auto checkProgram = [](GLuint program) {
            GLint result = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &result);
            if (result == GL_FALSE) {
                char buffer[1024];
                glGetProgramInfoLog(program, sizeof(buffer), nullptr, buffer);
                printf("ERROR PROGRAM LINK failed\n%s", buffer);
                exit(-2);
            }
        };

        { // Rendering (to display a single texture)
            const char* vertex = R"(
			#version 460 core
			layout (location=0) out vec2 uv;

			const vec2 pos[4] = vec2[4](
				vec2(-1.0, -1.0),
				vec2( 1.0, -1.0),
				vec2( 1.0,  1.0),
				vec2(-1.0,  1.0)
			);
			const uint index[6] = {0,1,2,0,2,3};
			void main() {
				vec2 v = pos[index[gl_VertexID]];
				gl_Position = vec4(v, 0.0, 1.0);
				uv = v*0.5+0.5;
			}
			)";

            const char* fragment = R"(
			#version 460 core
			layout (location=0) in vec2 uv;
			layout (location=0) out vec4 color;
			layout (binding = 0) uniform sampler2D texture0;
			void main() {
				color = texture(texture0, uv);
			}
			)";

            const GLuint sv = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(sv, 1, &vertex, nullptr);
            glCompileShader(sv);
            checkShader(sv);

            const GLuint sf = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(sf, 1, &fragment, nullptr);
            glCompileShader(sf);
            checkShader(sf);

            render.program = glCreateProgram();
            glAttachShader(render.program, sv);
            glAttachShader(render.program, sf);
            glLinkProgram(render.program);
            checkProgram(render.program);

            glGenVertexArrays(1, &render.vao);
        }

        // Init compute shader part
        {
            GLuint t = 0;
            glCreateTextures(GL_TEXTURE_2D, 1, &t);
            glTextureParameteri(t, GL_TEXTURE_MAX_LEVEL, 0);
            glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureStorage2D(t, 1, GL_RGBA32F, 512, 512);
            compute.texture = t;

            const char* code = R"(
                #version 460
                layout(local_size_x = 1, local_size_y = 1) in;
                layout(rgba32f, binding = 0) uniform image2D color;
                void main() {
                    ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
                    ivec2 size = imageSize(color);
                    vec4 fragment = vec4(float(uv.x)/float(size.x), float(uv.y)/float(size.y), 0.2, 1.0);

                    imageStore(color, uv, fragment);
                }
            )";

            GLuint shader  = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(shader, 1, &code, nullptr);
            glCompileShader(shader);
            checkShader(shader);
            compute.program = glCreateProgram();
            glAttachShader(compute.program, shader);
            glLinkProgram(compute.program);
            checkProgram(compute.program);
        }
    }


    void begin(const ImDrawCmd *cmd) {
        // save gl state
        glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*) &save.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*) &save.vao);
        glBindVertexArray(render.vao);

        // adjust viewport
		const float dpi = sapp_dpi_scale();
		const ImVec2 size = ImGui::GetIO().DisplaySize;
		const float w = cmd->ClipRect.w - cmd->ClipRect.y;
		glViewport(cmd->ClipRect.x * dpi, (size.y - cmd->ClipRect.y - w) * dpi, (cmd->ClipRect.z - cmd->ClipRect.x) * dpi, w * dpi);
    }
    void end() {
        // restore
        glBindVertexArray(save.vao);
        glUseProgram(save.program);
    }

} gpu;

void onInit() {
    gladLoadGL();
    gpu.init();
}

void frame() {
    ImGui::SetNextWindowSize({ 512,512 });
    if (ImGui::Begin("TEST", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList* parent_list, const ImDrawCmd* cmd) {
            gpu.begin(cmd);
            // COMPUTE
            glBindImageTexture(0, gpu.compute.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glUseProgram(gpu.compute.program);
            glDispatchCompute(512, 512, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            // RENDER
            glUseProgram(gpu.render.program);
            glBindTextureUnit(0, gpu.compute.texture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            gpu.end();
        }, nullptr);
        ImGui::End();
    }
}


int main(int, char **) {
    // when ready start the UI (this will not return until the app finishes)
    int imgui_flags = 0;
    #ifdef IMGUI_HAS_DOCK
    imgui_flags = ImGuiConfigFlags_DockingEnable;
    #endif

    imgui_app(frame, [](sapp_desc *desc) {
        static auto chainedInit = desc->init_cb;
        desc->width = 800;
        desc->height = 600;
        desc->window_title = "GL Compute demo";
        desc->high_dpi = true;
        desc->init_cb = [] {
            // Call first imgui_app initi
            chainedInit();
            // Call our onInit next
            onInit();
        };
    }, imgui_flags );
    return 0;
}
