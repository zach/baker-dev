// What a stack's game supplies (game-stacks.md §2, stacks/agent.md). No window, renderer,
// or SDL here: the play loop (app.hpp) and the host kit's headless `sim` both drive a Game,
// and the simulation, its tests, and the tool commands link without a window.
#pragma once

#include <baker/config.hpp>
#include <baker/script.hpp>

#include <cstdint>

union SDL_Event;

namespace base {

class App;
class Renderer;

class Game {
public:
    virtual ~Game() = default;
    // Binds the game's script functions and objects, before the data loads, so check
    // knows them.
    virtual void bind(baker::Context& ctx) { (void)ctx; }
    // Drops what the game keeps in the Context (objects, schedulers, tasks), before the
    // Context goes: after play ends and after each agent command. The game may be bound to
    // another Context afterwards.
    virtual void unbind(baker::Context& ctx) { (void)ctx; }
    // Reads what the game keeps from the data: after the load and after every change to the
    // layer stack (a reload, a toggle), once the layers' #autorun commands have run. Tweaks
    // are bound already; this is for structured data (a roster, an economy) the game copies
    // into its own arrays. A value tuned in a panel changes a tweak, not this.
    virtual void load(const baker::Config& cfg) { (void)cfg; }
    // Puts the game in its starting state, before play and before each simulated run. The
    // seed is the run's: a game that draws random numbers seeds them from it.
    virtual void reset(uint64_t seed) { (void)seed; }
    // After the data loaded, when playing; false stops the app.
    virtual bool start(App& app) {
        (void)app;
        return true;
    }
    // One fixed simulation step of `dt` seconds. No rendering here.
    virtual void step(double dt) = 0;
    // Draws the state, `alpha` of the way from the previous step to the last.
    virtual void draw(Renderer& r, float alpha) = 0;
    // The game's own ImGui windows: ui() every frame (a game played with the mouse), tools()
    // only while the panels show (F1).
    virtual void ui() {}
    virtual void tools() {}
    virtual void event(const SDL_Event& e) { (void)e; }
};

}  // namespace base
