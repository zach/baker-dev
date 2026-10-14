#include "base/app.hpp"

#include "console.hpp"  // Moira's examples/remote-console
#include "panels.hpp"   // Moira's examples/harness-imgui

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <moira/task.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace base {
namespace {

[[noreturn]] void usage(const char* program, const char* problem) {
    std::fprintf(stderr,
                 "%s\nusage: %s [--data DIR] [--renderer auto|noop|d3d11|d3d12|vulkan|gl]\n"
                 "          [--frames N] [--headless] [--console-port N] [--no-vsync]\n"
                 "          [--step-hz N] [--no-tools] [--screenshot PATH]\n",
                 problem, program);
    std::exit(2);
}

long number(const char* program, const char* text, long lo, long hi) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (!*text || *end || v < lo || v > hi) usage(program, "bad number");
    return v;
}

void native_handles(SDL_Window* window, void*& nwh, void*& ndt) {
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
#ifdef _WIN32
    nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    ndt = nullptr;
#else
    ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    nwh = reinterpret_cast<void*>(
        static_cast<uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
#endif
}

// Where a panel starts, as fractions of the window; collapsed panels show only their title.
void place(float x, float y, float w, float h, bool collapsed = false) {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(x * size.x, y * size.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w * size.x, h * size.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(collapsed, ImGuiCond_FirstUseEver);
}

// The edit-to-see time of the newest reload that changed something, in milliseconds;
// negative if there is none yet.
double last_edit_to_see_ms(const moira::Config& cfg, std::string& file) {
    double ms = -1;
    cfg.each_reload([&](const moira::ReloadRecord& r) {
        if (r.outcome != moira::ReloadOutcome::Ok || r.origin_ns == 0 || r.files_reloaded == 0)
            return;
        const uint64_t seen = r.visible_ns ? r.visible_ns : r.end_ns;
        ms = seen > r.origin_ns ? static_cast<double>(seen - r.origin_ns) / 1e6 : 0;
        const std::string_view path = r.first_file.str();
        const size_t slash = path.find_last_of("/\\");
        file = std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
    });
    return ms;
}

struct Tools {
    harness::TweaksState tweaks;
    harness::ShapesState shapes;
    harness::ConsoleState console;
    harness::LayersState layers;
    harness::SnapshotsState snapshots;
    bool shown = true;

    // Tuning on the left, the loop and the layers on the right, the scene between; the
    // panels used less start collapsed.
    void draw(App& app) {
        place(0.01f, 0.08f, 0.28f, 0.50f);
        harness::tweaks_panel(app.config(), moira::TweakRegistry::global(), tweaks);
        place(0.01f, 0.60f, 0.28f, 0.38f);
        harness::console_panel(app.context(), console);
        place(0.71f, 0.08f, 0.28f, 0.34f);
        harness::loop_panel(app.config());
        place(0.71f, 0.44f, 0.28f, 0.30f);
        harness::layers_panel(app.config(), layers);
        place(0.71f, 0.76f, 0.28f, 0.22f);
        harness::journal_panel(app.config());
        place(0.30f, 0.86f, 0.20f, 0.30f, true);
        harness::shapes_panel(app.config(), shapes);
        place(0.50f, 0.86f, 0.20f, 0.12f, true);
        harness::snapshots_panel(app.config(), snapshots);
    }
};

}  // namespace

AppOptions parse_args(int argc, char** argv, AppOptions o) {
    // A packaged game carries its data beside the executable; a build in the source tree
    // uses the game's source data (the default passed in). --data overrides both.
    if (const char* base = SDL_GetBasePath()) {
        std::error_code ec;
        const std::filesystem::path beside = std::filesystem::path(base) / "data";
        if (std::filesystem::exists(beside / "default.dta", ec)) o.data_dir = beside.string();
    }
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool more = i + 1 < argc;
        if (!std::strcmp(a, "--data") && more) {
            o.data_dir = argv[++i];
        } else if (!std::strcmp(a, "--renderer") && more) {
            if (!parse_renderer(argv[++i], o.renderer)) usage(argv[0], "unknown renderer");
        } else if (!std::strcmp(a, "--frames") && more) {
            o.frames = static_cast<uint32_t>(number(argv[0], argv[++i], 0, 100000000));
        } else if (!std::strcmp(a, "--headless")) {
            o.headless = true;
        } else if (!std::strcmp(a, "--console-port") && more) {
            o.console_port = static_cast<uint16_t>(number(argv[0], argv[++i], 0, 65535));
        } else if (!std::strcmp(a, "--no-vsync")) {
            o.vsync = false;
        } else if (!std::strcmp(a, "--step-hz") && more) {
            o.step_hz = static_cast<double>(number(argv[0], argv[++i], 1, 10000));
        } else if (!std::strcmp(a, "--no-tools")) {
            o.tools = false;
        } else if (!std::strcmp(a, "--screenshot") && more) {
            o.screenshot = argv[++i];
        } else {
            usage(argv[0], "unknown argument");
        }
    }
    if (o.headless) o.renderer = RendererKind::Noop;
    return o;
}

int run(Game& game, const AppOptions& options) {
    // App holds the Context, the Config, and the renderer: too big for the stack frame.
    std::unique_ptr<App> holder(new App(options));
    App& app = *holder;

    // The data, before any window: a broken file fails fast, with its file and line.
    game.bind(app.ctx_);
    struct Unbind {
        Game& game;
        moira::Context& ctx;
        ~Unbind() { game.unbind(ctx); }
    } unbind{game, app.ctx_};
    const bool loaded =
        app.live_.open({.data_dir = options.data_dir,
                        .changed = [&game](const moira::Config& c) { game.load(c); }});
    std::vector<std::string> log;
    if (app.live_.take_log(log))
        for (const std::string& line : log) std::fprintf(stderr, "%s\n", line.c_str());
    log.clear();
    if (!loaded) return 1;

    if (options.headless) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window =
        SDL_CreateWindow(options.title.c_str(), options.width, options.height,
                         SDL_WINDOW_RESIZABLE | (options.headless ? SDL_WINDOW_HIDDEN : 0));
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    void* nwh = nullptr;
    void* ndt = nullptr;
    if (!options.headless) native_handles(window, nwh, ndt);
    int width = options.width, height = options.height;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (!app.renderer_.init(nwh, ndt, width, height, options.renderer, options.vsync)) {
        std::fprintf(stderr, "bgfx could not start the renderer\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::printf("renderer: %s\ndata: %s\n", app.renderer_.name(), options.data_dir.c_str());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendRendererName = "base_bgfx";
    io.BackendFlags |=
        ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui_ImplSDL3_InitForOther(window);

    std::unique_ptr<Tools> tools(new Tools);
    tools->shown = options.tools;
    std::unique_ptr<Console> console;
    if (options.console_port) {
        sockets_start();
        console.reset(new Console(app.ctx_));
        if (console->listen(options.console_port))
            std::printf("console: 127.0.0.1:%u\n", unsigned{console->port()});
        else
            console.reset();
    }

    game.reset(0);
    int exit_code = game.start(app) ? 0 : 1;
    app.clock_.step_ns = FixedStep::hz(options.step_hz);
    uint64_t last = SDL_GetTicksNS();
    float fps = 0;
    std::string seen_file;
    uint64_t shot_requested = 0;
    while (exit_code == 0 && !app.quit_) {
        for (SDL_Event e; SDL_PollEvent(&e);) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) app.quit_ = true;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1) tools->shown = !tools->shown;
            if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                app.renderer_.resize(e.window.data1, e.window.data2);
            game.event(e);
        }
        const uint64_t now = SDL_GetTicksNS();
        const uint64_t elapsed = now - last;
        last = now;
        if (elapsed > 0)
            fps = fps * 0.95f + static_cast<float>(1e9 / static_cast<double>(elapsed)) * 0.05f;

        app.live_.begin_frame();
        if (console) console->poll();
        for (int n = app.clock_.advance(elapsed); n > 0; --n) {
            game.step(app.clock_.step_seconds());
            app.ctx_.scheduler().tick(app.clock_.step_seconds());  // script tasks, in seconds
        }
        if (app.live_.take_log(log)) {
            for (std::string& line : log) {
                std::fprintf(stderr, "%s\n", line.c_str());
                tools->console.log.push_back(std::move(line));
            }
            log.clear();
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        game.ui();
        if (tools->shown) {
            tools->draw(app);
            game.tools();
        }
        ImGui::Render();

        app.renderer_.begin_frame();
        game.draw(app.renderer_, app.clock_.alpha());
        app.renderer_.text(1, 1, "%s  %s  %.0f fps  F1: tools", options.title.c_str(),
                           app.renderer_.name(), double(fps));
        if (const double ms = last_edit_to_see_ms(app.config(), seen_file); ms >= 0)
            app.renderer_.text(1, 2, "edit to see: %.0f ms (%s)", ms, seen_file.c_str());
        app.renderer_.imgui(ImGui::GetDrawData());
        app.renderer_.end_frame();
        app.live_.end_frame();

        ++app.frame_;
        // A screenshot is of the last frame of a run with --frames, else of frame 30. It
        // lands a few frames later: a run with one pending waits up to ten more for it.
        const bool shot_pending =
            !options.screenshot.empty() && app.renderer_.screenshots_written() == 0;
        if (shot_pending && !shot_requested &&
            (options.frames ? app.frame_ >= options.frames : app.frame_ == 30)) {
            app.renderer_.screenshot(options.screenshot.c_str());
            shot_requested = app.frame_;
        }
        if (options.frames && app.frame_ >= options.frames &&
            (!shot_pending || app.frame_ > shot_requested + 10))
            app.quit_ = true;
        // Headless and open-ended (serving the console), nothing presents to pace the loop.
        if (options.headless && !options.frames) SDL_Delay(1);
    }

    console.reset();
    if (options.console_port) sockets_stop();
    app.renderer_.shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}

}  // namespace base
