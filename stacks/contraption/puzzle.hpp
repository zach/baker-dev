// Contraption's puzzles (game-stacks.md §4.1): levels of pieces, a tray of pieces to place,
// a goal, and the analyses the agent runs: does the intended solution solve, how robust is
// it to imprecise placement, and is there a cheaper solution (cheese). Side-on: every
// moving body is locked to the x-y plane and turns about z. No window here.
//
// A run is one Jolt world (base::Physics), built from the level and the placements in a
// fixed order and stepped at a fixed rate, so it replays bit for bit (§3.1).
#pragma once

#include <base/physics.hpp>

#include <baker/array.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace contraption {

enum class Shape : uint8_t { Box, Sphere, Bucket };

// A kind of piece, from (kinds ...): its shape and its physics.
struct Kind {
    std::string name;
    Shape shape = Shape::Box;
    float w = 1, h = 1;  // metres; a sphere's w is its diameter; a bucket's outer size
    float mass = 0;      // 0: static
    float friction = 0.5f, bounce = 0.1f;
    bool hinge = false;  // pinned at its center to the world: a seesaw
};

// A piece in a level, or a placement from the tray.
struct Piece {
    std::string name;
    int kind = -1;
    float x = 0, y = 0;  // metres, its center
    float angle = 0;     // degrees, about z
    bool fixed = false;  // static whatever its kind's mass
};

struct Level {
    std::string name, desc;
    std::vector<Piece> pieces;
    std::vector<std::pair<int, int>> tray;  // kind, count
    std::vector<Piece> intended;            // the designer's solution, from the tray
    baker::Node goal;                       // a command; true when solved
    float bounds[4] = {-8, 0, 8, 8};        // x0 y0 x1 y1: where the tray may be placed
};

struct Catalog {
    std::vector<Kind> kinds;
    std::vector<Level> levels;
    baker::Ref<baker::Array> tree;  // keeps the goal commands alive

    [[nodiscard]] int kind(const std::string& name) const;
    [[nodiscard]] int level(const std::string& name) const;
};

struct Rules {
    float step_hz = 60;
    float run_seconds = 12;     // an attempt fails if the goal has not held by then
    float hold_seconds = 0.5f;  // the goal must hold this long
    float grid = 0.05f;         // metres: where a mouse can put a piece
    float angle_step = 5;       // degrees
};

// Reads (kinds ...) and (levels ...). False, with the reason, on an unknown kind.
[[nodiscard]] bool load(const baker::Ref<baker::Array>& root, Catalog& out, std::string& error);

// A level with its placements, in a Jolt world: bodies created in a fixed order.
class Run {
public:
    Run(const Catalog& catalog, const Level& level, const std::vector<Piece>& placed);
    void step(float dt);
    [[nodiscard]] base::Physics& physics() noexcept { return world_; }

    // Where a piece (of the level, then of the placements) is now.
    struct Pose {
        float x, y, angle_rad;
        float quat[4];
    };
    [[nodiscard]] size_t count() const noexcept { return bodies_.size(); }
    [[nodiscard]] Pose pose(size_t i) const;
    [[nodiscard]] const Piece& piece(size_t i) const { return *pieces_[i]; }
    // The piece named `name`, or -1.
    [[nodiscard]] int find(const std::string& name) const;
    // True if piece `a`'s center is inside the bucket `b`'s hollow.
    [[nodiscard]] bool inside(int a, int b) const;

private:
    const Catalog& catalog_;
    base::Physics world_;
    std::vector<const Piece*> pieces_;
    std::vector<JPH::BodyID> bodies_;
};

// Whether the goal holds now, about a run: the game evaluates the level's goal command.
using GoalTest = std::function<bool(Run&)>;

struct Attempt {
    bool solved = false;
    float seconds = 0;  // when the goal began to hold
    int steps = 0;
};
// Runs the level with `placed` until the goal holds for hold_seconds or run_seconds pass.
[[nodiscard]] Attempt attempt(const Catalog& catalog, const Level& level,
                              const std::vector<Piece>& placed, const Rules& rules,
                              const GoalTest& goal);

// A placement snapped as a mouse would place it: to the grid and the angle step.
[[nodiscard]] Piece snapped(Piece p, const Rules& rules);

// The share of `samples` perturbed intended solutions that still solve: each piece moved by
// up to `metres` and turned by up to `degrees`, either way, uniformly. Seeded.
[[nodiscard]] double robustness(const Catalog& catalog, const Level& level, const Rules& rules,
                                const GoalTest& goal, int samples, float metres, float degrees,
                                uint64_t seed);

struct Cheese {
    int fewest = -1;           // the fewest tray pieces that solved; -1: none found below intended
    std::vector<Piece> found;  // that solution
    int tried = 0;
};
// Searches for solutions with fewer tray pieces than the intended one: no pieces, then
// random placements of 1, 2, ... pieces from the tray inside the level's bounds, `tries`
// for each count. Seeded.
[[nodiscard]] Cheese cheese(const Catalog& catalog, const Level& level, const Rules& rules,
                            const GoalTest& goal, int tries, uint64_t seed);

// A placement as data, as an agent would write it into a scenario.
[[nodiscard]] std::string to_dta(const std::vector<Piece>& placed, const Catalog& catalog);

}  // namespace contraption
