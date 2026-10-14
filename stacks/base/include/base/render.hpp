// The base's renderer: bgfx under the little a prototype needs (game-stacks.md §2.1): lit
// boxes in view 0, Dear ImGui over them in the last view, and bgfx's debug text. A stack
// that needs more adds it here, once, for every stack. No bgfx type appears in this header.
//
// Shaders are bgfx's own, precompiled for every renderer (its debug-draw lit fill and its
// ImGui shader), so the build runs no shader compiler.
#pragma once

#include <cstdint>

struct ImDrawData;

namespace base {

enum class RendererKind : uint8_t { Auto, Noop, Direct3D11, Direct3D12, Vulkan, OpenGL };

// Parses "auto", "noop", "d3d11", "d3d12", "vulkan", or "gl"; false if it is none of them.
[[nodiscard]] bool parse_renderer(const char* name, RendererKind& out) noexcept;

struct Camera {
    float eye[3] = {0, 2, -6};
    float at[3] = {0, 0, 0};
    float fov_y_degrees = 60;
    float near_plane = 0.1f, far_plane = 500;
};

struct Box {
    float center[3] = {0, 0, 0};
    float size[3] = {1, 1, 1};      // full extents, in meters
    float rotation[3] = {0, 0, 0};  // about x, y, z, in radians
    float color[4] = {1, 1, 1, 1};
    // x y z w; when w is not 0 it replaces `rotation` (a physics body's orientation).
    float quat[4] = {0, 0, 0, 0};
    bool sphere = false;  // an ellipsoid filling the box: a ball, a wheel
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // `window` and `display` are the native handles (HWND; X11 Window and Display*); both
    // may be null for the Noop renderer. False if bgfx cannot start.
    [[nodiscard]] bool init(void* window, void* display, int width, int height, RendererKind kind,
                            bool vsync);
    void shutdown();
    void resize(int width, int height);

    // The app opens and submits each frame; the game, between, sets the camera and draws.
    void begin_frame();
    void camera(const Camera& c);
    void clear_color(float r, float g, float b);
    void box(const Box& b);
    // A line of debug text at a cell (8 by 16 pixels): a HUD that needs no ImGui.
    void text(int column, int row, const char* format, ...);
    // Dear ImGui's draw data for this frame, drawn over the scene.
    void imgui(ImDrawData* data);
    // Submits the frame. Returns bgfx's frame number.
    uint32_t end_frame();
    // Asks for the back buffer as a PNG at `path`. It is written a frame or two later, from
    // within end_frame(); screenshots_written() counts them. Nothing under Noop.
    void screenshot(const char* path);
    [[nodiscard]] uint32_t screenshots_written() const noexcept;

    [[nodiscard]] const char* name() const noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    void imgui_textures(ImDrawData* data);

    struct Callback;
    Callback* callback_ = nullptr;
    bool ready_ = false;
    int width_ = 0, height_ = 0;
    bool vsync_ = true;
    // bgfx handles, as their indices; 0xffff is none.
    uint16_t box_program_ = 0xffff, box_vertices_ = 0xffff, box_indices_ = 0xffff;
    uint16_t sphere_vertices_ = 0xffff, sphere_indices_ = 0xffff;
    uint16_t params_ = 0xffff;
    uint16_t imgui_program_ = 0xffff, imgui_sampler_ = 0xffff;
};

}  // namespace base
