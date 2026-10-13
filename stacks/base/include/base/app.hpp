// The base's application shell (game-stacks.md §2): an SDL3 window, the bgfx renderer,
// Moira's live data loop, its tuning panels over Dear ImGui (F1), and its remote console on
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
#include "base/live.hpp"
#include "base/render.hpp"

#include <moira/script.hpp>

#include <cstdint>
#include <string>

union SDL_Event;

namespace base {

struct AppOptions {
    std::string title = "Moira stack";
    std::string data_dir{};  // the game's data: default.dta, local.dta, agent.dta
    int width = 1280, height = 720;
    RendererKind renderer = RendererKind::Auto;
    bool vsync = true;
    double step_hz = 60;           // the simulation's fixed rate
    uint32_t frames = 0;           // exit after this many frames; 0: run until closed
    bool headless = false;         // no window (SDL's dummy video driver) and the Noop renderer
    uint16_t console_port = 4555;  // the remote console on 127.0.0.1; 0: none
    bool tools = true;             // the panels start shown (F1 toggles them)
    std::string screenshot{};  // a PNG of the last frame (of frame 30 without frames); empty: none
};

// A `data` directory beside the executable, if it holds default.dta, replaces the default
// data_dir: that is how a packaged game finds its data. Then reads --data DIR, --renderer NAME,
// --frames N, --headless, --console-port N, --no-vsync,
// --step-hz N, --no-tools, and --screenshot PATH over `defaults`. Prints usage and exits on a bad
// argument.
[[nodiscard]] AppOptions parse_args(int argc, char** argv, AppOptions defaults);

class App;

class Game {
public:
    virtual ~Game() = default;
    // Binds the game's script functions and objects, before the data loads, so check
    // knows them.
    virtual void bind(moira::Context& ctx) { (void)ctx; }
    // After the data loaded; false stops the app.
    virtual bool start(App& app) {
        (void)app;
        return true;
    }
    // One fixed simulation step of `dt` seconds. No rendering here.
    virtual void step(double dt) = 0;
    // Draws the state, `alpha` of the way from the previous step to the last.
    virtual void draw(Renderer& r, float alpha) = 0;
    // The game's own ImGui windows, if any, shown with the panels.
    virtual void tools() {}
    virtual void event(const SDL_Event& e) { (void)e; }
};

class App {
public:
    [[nodiscard]] moira::Context& context() noexcept { return ctx_; }
    [[nodiscard]] Live& live() noexcept { return live_; }
    [[nodiscard]] moira::Config& config() noexcept { return live_.config(); }
    [[nodiscard]] Renderer& renderer() noexcept { return renderer_; }
    [[nodiscard]] const AppOptions& options() const noexcept { return options_; }
    [[nodiscard]] uint64_t frame() const noexcept { return frame_; }
    // Ends the run after the current frame.
    void quit() noexcept { quit_ = true; }

private:
    friend int run(Game& game, const AppOptions& options);
    explicit App(const AppOptions& options) : options_(options), live_(ctx_) {}

    AppOptions options_;
    moira::Context ctx_;
    Live live_;
    Renderer renderer_;
    FixedStep clock_;
    uint64_t frame_ = 0;
    bool quit_ = false;
};

// Runs the game until its window closes, `frames` have passed, or it quits. Returns the
// process's exit code.
int run(Game& game, const AppOptions& options);

}  // namespace base
