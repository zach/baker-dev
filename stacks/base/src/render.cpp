#include "base/render.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bimg/bimg.h>
#include <bx/file.h>
#include <bx/math.h>
#include <imgui.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// bgfx's precompiled shaders (BSD-2), from its examples.
#include "debugdraw/fs_debugdraw_fill_lit.bin.h"
#include "debugdraw/vs_debugdraw_fill_lit_mesh.bin.h"
#include "imgui/fs_ocornut_imgui.bin.h"
#include "imgui/vs_ocornut_imgui.bin.h"

namespace base {
namespace {

const bgfx::EmbeddedShader kShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_debugdraw_fill_lit_mesh),
    BGFX_EMBEDDED_SHADER(fs_debugdraw_fill_lit),
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END(),
};

constexpr bgfx::ViewId kSceneView = 0;
constexpr bgfx::ViewId kImGuiView = 255;

// A unit cube; the fill shader computes normals from screen derivatives, so corners are
// shared.
const float kCubeVertices[8][3] = {
    {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
    {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f},  {0.5f, 0.5f, 0.5f}};
const uint16_t kCubeIndices[36] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 4, 2, 4, 6, 2,
                                   1, 3, 5, 5, 3, 7, 0, 1, 4, 4, 1, 5, 2, 6, 3, 6, 7, 3};

// A unit sphere (radius 0.5), as latitude rings; built once.
constexpr int kRings = 10, kSegments = 16;
struct SphereMesh {
    float vertices[(kRings + 1) * (kSegments + 1)][3];
    uint16_t indices[kRings * kSegments * 6];
    SphereMesh() {
        int v = 0, i = 0;
        for (int r = 0; r <= kRings; ++r) {
            const float lat = 3.14159265f * (float(r) / kRings - 0.5f);
            for (int s = 0; s <= kSegments; ++s) {
                const float lon = 6.28318531f * float(s) / kSegments;
                vertices[v][0] = 0.5f * std::cos(lat) * std::cos(lon);
                vertices[v][1] = 0.5f * std::sin(lat);
                vertices[v][2] = 0.5f * std::cos(lat) * std::sin(lon);
                ++v;
            }
        }
        for (int r = 0; r < kRings; ++r)
            for (int s = 0; s < kSegments; ++s) {
                const auto a = static_cast<uint16_t>(r * (kSegments + 1) + s);
                const auto b = static_cast<uint16_t>(a + kSegments + 1);
                const uint16_t quad[6] = {a,
                                          b,
                                          static_cast<uint16_t>(a + 1),
                                          static_cast<uint16_t>(a + 1),
                                          b,
                                          static_cast<uint16_t>(b + 1)};
                for (uint16_t q : quad) indices[i++] = q;
            }
    }
};
const SphereMesh& sphere_mesh() {
    static const SphereMesh m;
    return m;
}

bgfx::VertexLayout box_layout() {
    bgfx::VertexLayout l;
    l.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    return l;
}

bgfx::VertexLayout imgui_layout() {
    bgfx::VertexLayout l;
    l.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return l;
}

bgfx::RendererType::Enum to_bgfx(RendererKind k) {
    switch (k) {
        case RendererKind::Noop: return bgfx::RendererType::Noop;
        case RendererKind::Direct3D11: return bgfx::RendererType::Direct3D11;
        case RendererKind::Direct3D12: return bgfx::RendererType::Direct3D12;
        case RendererKind::Vulkan: return bgfx::RendererType::Vulkan;
        case RendererKind::OpenGL: return bgfx::RendererType::OpenGL;
        case RendererKind::Auto: break;
    }
    return bgfx::RendererType::Count;  // bgfx picks the platform's best
}

uint32_t pack_rgb(const float rgb[3]) {
    auto c = [](float v) {
        return static_cast<uint32_t>(bx::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return c(rgb[0]) << 24 | c(rgb[1]) << 16 | c(rgb[2]) << 8 | 0xffu;
}

// ImGui texture ids are bgfx texture indices plus one: ImGui reserves 0 for "none".
ImTextureID to_id(bgfx::TextureHandle h) {
    return static_cast<ImTextureID>(h.idx) + 1;
}
bgfx::TextureHandle from_id(ImTextureID id) {
    return {static_cast<uint16_t>(id - 1)};
}

}  // namespace

// What bgfx reports back: fatal errors, and screenshots, written as PNG.
struct Renderer::Callback final : bgfx::CallbackI {
    uint32_t written = 0;

    void fatal(const char* file, uint16_t line, bgfx::Fatal::Enum code, const char* text) override {
        std::fprintf(stderr, "bgfx fatal %d at %s:%u: %s\n", int(code), file, unsigned{line}, text);
        std::abort();
    }
    void traceVargs(const char* file, uint16_t line, const char* format, va_list args) override {
        if (!std::getenv("STACKS_BGFX_TRACE")) return;
        std::fprintf(stderr, "%s:%u: ", file, unsigned{line});
        std::vfprintf(stderr, format, args);
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* path, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum format, const void* data, uint32_t,
                    bool yflip) override {
        bx::FileWriter out;
        if (!bx::open(&out, path)) {
            std::fprintf(stderr, "screenshot: cannot write %s\n", path);
            return;
        }
        bimg::imageWritePng(&out, width, height, pitch, data, bimg::TextureFormat::Enum(format),
                            yflip);
        bx::close(&out);
        ++written;
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

bool parse_renderer(const char* name, RendererKind& out) noexcept {
    struct Name {
        const char* text;
        RendererKind kind;
    };
    static constexpr Name kNames[] = {
        {"auto", RendererKind::Auto},        {"noop", RendererKind::Noop},
        {"d3d11", RendererKind::Direct3D11}, {"d3d12", RendererKind::Direct3D12},
        {"vulkan", RendererKind::Vulkan},    {"gl", RendererKind::OpenGL}};
    for (const Name& n : kNames)
        if (std::strcmp(name, n.text) == 0) {
            out = n.kind;
            return true;
        }
    return false;
}

bool Renderer::init(void* window, void* display, int width, int height, RendererKind kind,
                    bool vsync) {
    // Calling renderFrame before init keeps bgfx on this thread: one thread, no render
    // thread to reason about, until a stack measures that it needs one.
    bgfx::renderFrame();
    bgfx::Init init;
    init.type = to_bgfx(kind);
    init.swapChain.nwh = window;
    init.swapChain.ndt = display;
    init.swapChain.width = static_cast<uint32_t>(width);
    init.swapChain.height = static_cast<uint32_t>(height);
    init.reset = vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    if (!callback_) callback_ = new Callback;
    init.callback = callback_;
    if (!bgfx::init(init)) return false;
    ready_ = true;
    width_ = width;
    height_ = height;
    vsync_ = vsync;
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    box_program_ = bgfx::createProgram(
                       bgfx::createEmbeddedShader(kShaders, type, "vs_debugdraw_fill_lit_mesh"),
                       bgfx::createEmbeddedShader(kShaders, type, "fs_debugdraw_fill_lit"), true)
                       .idx;
    box_vertices_ =
        bgfx::createVertexBuffer(bgfx::makeRef(kCubeVertices, sizeof kCubeVertices), box_layout())
            .idx;
    box_indices_ = bgfx::createIndexBuffer(bgfx::makeRef(kCubeIndices, sizeof kCubeIndices)).idx;
    const SphereMesh& sphere = sphere_mesh();
    sphere_vertices_ = bgfx::createVertexBuffer(
                           bgfx::makeRef(sphere.vertices, sizeof sphere.vertices), box_layout())
                           .idx;
    sphere_indices_ =
        bgfx::createIndexBuffer(bgfx::makeRef(sphere.indices, sizeof sphere.indices)).idx;
    params_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, 4).idx;
    imgui_program_ =
        bgfx::createProgram(bgfx::createEmbeddedShader(kShaders, type, "vs_ocornut_imgui"),
                            bgfx::createEmbeddedShader(kShaders, type, "fs_ocornut_imgui"), true)
            .idx;
    imgui_sampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler).idx;
    return true;
}

Renderer::~Renderer() {
    shutdown();
    delete callback_;
}

void Renderer::shutdown() {
    if (!ready_) return;
    if (ImGui::GetCurrentContext())
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
            if (tex->TexID != ImTextureID_Invalid) {
                bgfx::destroy(from_id(tex->TexID));
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
    bgfx::destroy(bgfx::ProgramHandle{box_program_});
    bgfx::destroy(bgfx::VertexBufferHandle{box_vertices_});
    bgfx::destroy(bgfx::IndexBufferHandle{box_indices_});
    bgfx::destroy(bgfx::VertexBufferHandle{sphere_vertices_});
    bgfx::destroy(bgfx::IndexBufferHandle{sphere_indices_});
    bgfx::destroy(bgfx::UniformHandle{params_});
    bgfx::destroy(bgfx::ProgramHandle{imgui_program_});
    bgfx::destroy(bgfx::UniformHandle{imgui_sampler_});
    bgfx::shutdown();
    ready_ = false;
}

void Renderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    bgfx::SwapChain chain;
    chain.width = static_cast<uint32_t>(width);
    chain.height = static_cast<uint32_t>(height);
    bgfx::reset(vsync_ ? BGFX_RESET_VSYNC : BGFX_RESET_NONE, &chain);
}

void Renderer::begin_frame() {
    bgfx::setViewRect(kSceneView, 0, 0, static_cast<uint16_t>(width_),
                      static_cast<uint16_t>(height_));
    camera(Camera{});
    clear_color(0.1f, 0.1f, 0.12f);
    bgfx::touch(kSceneView);
    bgfx::dbgTextClear();
}

void Renderer::clear_color(float r, float g, float b) {
    const float rgb[3] = {r, g, b};
    bgfx::setViewClear(kSceneView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, pack_rgb(rgb), 1.0f, 0);
}

void Renderer::camera(const Camera& camera) {
    float view[16], proj[16];
    bx::mtxLookAt(view, bx::Vec3(camera.eye[0], camera.eye[1], camera.eye[2]),
                  bx::Vec3(camera.at[0], camera.at[1], camera.at[2]));
    const float aspect =
        height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
    bx::mtxProj(proj, camera.fov_y_degrees, aspect, camera.near_plane, camera.far_plane,
                bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(kSceneView, view, proj);
}

void Renderer::box(const Box& b) {
    float model[16];
    if (b.quat[3] != 0) {
        // Rows are the rotated axes, scaled (bx's row-vector convention), then translation.
        const float x = b.quat[0], y = b.quat[1], z = b.quat[2], w = b.quat[3];
        const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)},
                               {2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)},
                               {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)}};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) model[row * 4 + col] = r[row][col] * b.size[row];
            model[row * 4 + 3] = 0;
        }
        model[12] = b.center[0];
        model[13] = b.center[1];
        model[14] = b.center[2];
        model[15] = 1;
    } else {
        bx::mtxSRT(model, b.size[0], b.size[1], b.size[2], b.rotation[0], b.rotation[1],
                   b.rotation[2], b.center[0], b.center[1], b.center[2]);
    }
    // The lit fill shader's parameters: light direction and shininess, sky color, ground
    // color, material color.
    const float params[16] = {0.4f,       0.8f,       -0.45f,     1.0f,      1.0f,  1.0f,
                              1.0f,       0.0f,       0.3f,       0.3f,      0.35f, 0.0f,
                              b.color[0], b.color[1], b.color[2], b.color[3]};
    bgfx::setTransform(model);
    bgfx::setUniform(bgfx::UniformHandle{params_}, params, 4);
    bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{b.sphere ? sphere_vertices_ : box_vertices_});
    bgfx::setIndexBuffer(bgfx::IndexBufferHandle{b.sphere ? sphere_indices_ : box_indices_});
    bgfx::setState(BGFX_STATE_DEFAULT);
    bgfx::submit(kSceneView, bgfx::ProgramHandle{box_program_});
}

void Renderer::box(float x, float y, float z, float w, float h, float d, float r, float g, float b,
                   float yaw) {
    Box bx;
    bx.center[0] = x;
    bx.center[1] = y;
    bx.center[2] = z;
    bx.size[0] = w;
    bx.size[1] = h;
    bx.size[2] = d;
    bx.rotation[1] = yaw;
    bx.color[0] = r;
    bx.color[1] = g;
    bx.color[2] = b;
    box(bx);
}

void Renderer::text(int column, int row, const char* format, ...) {
    va_list args;
    va_start(args, format);
    bgfx::dbgTextPrintfVargs(static_cast<uint16_t>(column), static_cast<uint16_t>(row), 0x0f,
                             format, args);
    va_end(args);
}

void Renderer::imgui_textures(ImDrawData* data) {
    if (!data->Textures) return;
    for (ImTextureData* tex : *data->Textures) {
        switch (tex->Status) {
            case ImTextureStatus_WantCreate: {
                // Created empty, then filled: bgfx makes a texture given data at creation
                // immutable, and the glyphs ImGui adds later would never reach it.
                const auto w = static_cast<uint16_t>(tex->Width),
                           h16 = static_cast<uint16_t>(tex->Height);
                const bgfx::TextureHandle h =
                    bgfx::createTexture2D(w, h16, false, 1, bgfx::TextureFormat::RGBA8, 0, nullptr);
                bgfx::updateTexture2D(
                    h, 0, 0, 0, 0, w, h16,
                    bgfx::copy(tex->GetPixels(), static_cast<uint32_t>(tex->GetSizeInBytes())));
                tex->SetTexID(to_id(h));
                tex->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantUpdates: {
                const bgfx::TextureHandle h = from_id(tex->TexID);
                for (const ImTextureRect& r : tex->Updates) {
                    // bgfx copies rows from a pitch; give it the rectangle's rows packed.
                    const uint32_t row = static_cast<uint32_t>(r.w) * 4;
                    const bgfx::Memory* mem = bgfx::alloc(row * r.h);
                    for (int y = 0; y < r.h; ++y)
                        std::memcpy(mem->data + row * static_cast<uint32_t>(y),
                                    tex->GetPixelsAt(r.x, r.y + y), row);
                    bgfx::updateTexture2D(h, 0, 0, r.x, r.y, r.w, r.h, mem);
                }
                tex->SetStatus(ImTextureStatus_OK);
                break;
            }
            case ImTextureStatus_WantDestroy:
                if (tex->TexID != ImTextureID_Invalid) bgfx::destroy(from_id(tex->TexID));
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            default: break;
        }
    }
}

void Renderer::imgui(ImDrawData* data) {
    if (!data) return;
    imgui_textures(data);
    const float w = data->DisplaySize.x, h = data->DisplaySize.y;
    if (w <= 0 || h <= 0) return;
    float ortho[16];
    bx::mtxOrtho(ortho, data->DisplayPos.x, data->DisplayPos.x + w, data->DisplayPos.y + h,
                 data->DisplayPos.y, 0.0f, 1000.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewMode(kImGuiView, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(kImGuiView, nullptr, ortho);
    bgfx::setViewRect(kImGuiView, 0, 0, static_cast<uint16_t>(width_),
                      static_cast<uint16_t>(height_));

    static const bgfx::VertexLayout layout = imgui_layout();
    const ImVec2 clip_at = data->DisplayPos, clip_scale = data->FramebufferScale;
    for (const ImDrawList* list : data->CmdLists) {
        const auto vertices = static_cast<uint32_t>(list->VtxBuffer.size());
        const auto indices = static_cast<uint32_t>(list->IdxBuffer.size());
        if (bgfx::getAvailTransientVertexBuffer(vertices, layout) < vertices ||
            bgfx::getAvailTransientIndexBuffer(indices, sizeof(ImDrawIdx) == 4) < indices)
            break;  // out of transient space: drop the rest of this frame's UI
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, vertices, layout);
        bgfx::allocTransientIndexBuffer(&tib, indices, sizeof(ImDrawIdx) == 4);
        std::memcpy(tvb.data, list->VtxBuffer.Data, vertices * sizeof(ImDrawVert));
        std::memcpy(tib.data, list->IdxBuffer.Data, indices * sizeof(ImDrawIdx));
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                cmd.UserCallback(list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) continue;
            const float x0 = bx::max((cmd.ClipRect.x - clip_at.x) * clip_scale.x, 0.0f);
            const float y0 = bx::max((cmd.ClipRect.y - clip_at.y) * clip_scale.y, 0.0f);
            const float x1 = bx::min((cmd.ClipRect.z - clip_at.x) * clip_scale.x, 65535.0f);
            const float y1 = bx::min((cmd.ClipRect.w - clip_at.y) * clip_scale.y, 65535.0f);
            if (x1 <= x0 || y1 <= y0) continue;
            bgfx::setScissor(static_cast<uint16_t>(x0), static_cast<uint16_t>(y0),
                             static_cast<uint16_t>(x1 - x0), static_cast<uint16_t>(y1 - y0));
            bgfx::setState(
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::setTexture(0, bgfx::UniformHandle{imgui_sampler_}, from_id(cmd.GetTexID()));
            bgfx::setVertexBuffer(0, &tvb, cmd.VtxOffset, vertices);
            bgfx::setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
            bgfx::submit(kImGuiView, bgfx::ProgramHandle{imgui_program_});
        }
    }
}

uint32_t Renderer::end_frame() {
    return bgfx::frame();
}

void Renderer::screenshot(const char* path) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path);
}

uint32_t Renderer::screenshots_written() const noexcept {
    return callback_ ? callback_->written : 0;
}

const char* Renderer::name() const noexcept {
    return ready_ ? bgfx::getRendererName(bgfx::getRendererType()) : "none";
}

}  // namespace base
