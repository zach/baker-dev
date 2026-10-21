// The base's application shell (game-stacks.md §2): an SDL3 window, the bgfx renderer,
// Baker's live data loop, its tuning panels over Dear ImGui (F1), and its remote console on
// loopback. A game supplies a Game and calls run():
//
//   int main(int argc, char** argv) {
//       CubeGame game;
//       return base::run(game, base::parse_args(argc, argv, {.title = "Cube", .data_dir = ...}));
//   }
//
// The frame: events; Live::begin_frame (reload what was saved); fixed simulation steps;
// draw; the panels; present; Live::end_frame (the reload is now visible). The simulation
// (Game::step) sees no renderer, so a headless runner can drive the same code.
#pragma once

#include "base/fixed_step.hpp"
#include "base/game.hpp"
#include "base/live.hpp"
#include "base/render.hpp"

#include <baker/script.hpp>

#include <cstdint>
#include <string>

namespace base {

struct AppOptions {
    std::string title = "Baker stack";
    std::string data_dir{};  // the game's data: default.dta, local.dta, agent.dta
    int width = 1280, height = 720;
    RendererKind renderer = RendererKind::Auto;
    bool vsync = true;
    double step_hz = 60;  // the simulation's fixed rate
    // Frames a second at most: the loop sleeps out the rest of each frame. Pacing changes
    // when frames run, never the simulation's steps. Negative: the default, which paces a
    // headless game run without --frames (one serving its console) to step_hz, so that an
    // idle one costs next to nothing, and leaves every other run unpaced (a window presents
    // at vsync). 0: unpaced.
    double frame_hz = -1;
    uint32_t frames = 0;           // exit after this many frames; 0: run until closed
    bool headless = false;         // no window (SDL's dummy video driver) and the Noop renderer
    uint16_t console_port = 4555;  // the remote console on 127.0.0.1; 0: none. The stack's
                                   // project file's (console PORT) wins over this default
    bool console_write = false;    // the console may write live edits back (LiveOptions)
    bool tools = true;             // the panels start shown (F1 toggles them)
    std::string screenshot{};  // a PNG of the last frame (of frame 30 without frames); empty: none
};

// A `data` directory beside the executable, if it holds default.dta, replaces the default
// data_dir: that is how a packaged game finds its data. Then reads --data DIR, --renderer NAME,
// --frames N, --headless, --console-port N, --console-write, --no-vsync,
// --step-hz N, --frame-hz N, --no-tools, and --screenshot PATH over `defaults`. Prints usage and
// exits on a bad argument.
[[nodiscard]] AppOptions parse_args(int argc, char** argv, AppOptions defaults);

class App {
public:
    [[nodiscard]] baker::Context& context() noexcept { return ctx_; }
    [[nodiscard]] Live& live() noexcept { return live_; }
    [[nodiscard]] baker::Config& config() noexcept { return live_.config(); }
    [[nodiscard]] Renderer& renderer() noexcept { return renderer_; }
    [[nodiscard]] const AppOptions& options() const noexcept { return options_; }
    [[nodiscard]] uint64_t frame() const noexcept { return frame_; }
    // Ends the run after the current frame.
    void quit() noexcept { quit_ = true; }

private:
    friend int run(Game& game, const AppOptions& options);
    explicit App(const AppOptions& options) : options_(options), live_(ctx_) {}

    AppOptions options_;
    baker::Context ctx_;
    Live live_;
    Renderer renderer_;
    FixedStep clock_;
    uint64_t frame_ = 0;
    bool quit_ = false;
};

// The entry point of every stack's game (stacks/agent.md): with no command, plays (run);
// otherwise runs one of the agent's commands: sim, check, reference, diff, or promote.
// `defaults.data_dir` is the game's data/; its scenarios and objectives are in the sim/ and
// metrics/ directories beside it. Returns the process's exit code.
int main(Game& game, int argc, char** argv, AppOptions defaults);

// Runs the game until its window closes, `frames` have passed, or it quits. Returns the
// process's exit code.
int run(Game& game, const AppOptions& options);

}  // namespace base
