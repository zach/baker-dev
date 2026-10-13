// What a stack's game supplies (game-stacks.md §2, stacks/agent.md). No window, renderer,
// or SDL here: the play loop (app.hpp) and the headless simulation (sim.hpp) both drive a
// Game, and the simulation, its tests, and the tool commands link without a window.
#pragma once

#include <moira/script.hpp>

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
    virtual void bind(moira::Context& ctx) { (void)ctx; }
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
    // The game's own ImGui windows, if any, shown with the panels.
    virtual void tools() {}
    virtual void event(const SDL_Event& e) { (void)e; }
};

}  // namespace base
