// Wayfarer's world (game-stacks.md §4.4): cells of boxes, points of interest, traversal
// abilities, and missions with declarative objectives; and what the agent proves about them
// without playing: softlocks, reachability, and pacing. No window and no Jolt here.
//
// A mission states what must become true, not the steps (Halo 3's objectives):
//
//   (lighthouse_signal
//      (unlock {mission.done harbor_intro})
//      (objectives
//         (reach (goal {at_poi lighthouse}) (hint_after 120))
//         (light (goal {flag lamp_lit}) (requires reach)))
//      (reward (xp 300) (item spyglass)))
//
// The analyses read the goal commands as facts about the world: {at_poi p} is being at p,
// {flag f} is visiting a point that sets f, {has_item i} is holding i, {mission.done m} is
// m's objectives all done. Abilities (climb, glide) come from items, and decide which of the
// navigation mesh's links a path may take, so "reachable by then" is a path query.
#pragma once

#include <base/nav.hpp>

#include <moira/array.hpp>
#include <moira/config.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace wayfarer {

// A box of the world, from y to y + h. Climbable faces get climb links.
struct Block {
    float x = 0, z = 0, w = 1, d = 1, h = 1, y = 0;
    bool climb = false;
};

// A one-way move the mesh cannot show: a glide off a height, or a drop off a ledge.
struct Move {
    base::NavPoint from, to;
    bool glide = false;
};

struct Cell {
    std::string name;
    int cx = 0, cz = 0;
    std::vector<Block> blocks;
    std::vector<Move> moves;
    uint64_t version = 0;  // a hash of its source: what a reload compares
};

struct Poi {
    std::string name;
    base::NavPoint at;
    std::vector<int> gives;  // items, on a visit
    std::vector<int> needs;  // items a visit needs before it gives or sets anything
    int sets = -1;           // a flag, on a visit
    bool landmark = false;   // found, but nothing to do
};

// A goal command, as the analyses read it.
enum class Fact : uint8_t { AtPoi, Flag, HasItem, MissionDone, Other };
struct Goal {
    Fact fact = Fact::Other;
    int target = -1;      // a poi, flag, item, or mission
    moira::Node command;  // as written: the game evaluates it
};

struct Objective {
    std::string name;
    Goal goal;
    std::vector<int>
        after;  // objectives of the same mission that must be done first: (requires ...)
    float hint_after = -1;  // seconds
};

struct Mission {
    std::string name, region, file;
    std::vector<Goal> unlock;  // all must hold; none: open from the start
    std::vector<Objective> objectives;
    std::vector<int> reward_items;
    int xp = 0;
    float par = -1;  // seconds the designer expects, from the start of play; -1 none
};

struct Ability {
    std::string name;
    uint16_t flag = base::nav::kWalk;
    int item = -1;  // -1: always
};

// (wayfarer ...): traversal limits, shared by the game's character and the analyses.
struct Traversal {
    float run_speed = 6;         // m/s
    float climb_max = 8;         // metres a climb can go up without a ledge
    float climb_speed = 2.5f;    // m/s
    float glide_speed = 10;      // m/s forward
    float glide_sink = 2;        // m/s down
    float step = 0.5f;           // a step walked up without climbing
    float poi_radius = 4;        // metres: at a poi
    float discover_radius = 20;  // metres: a poi in sight is found
    [[nodiscard]] float glide_ratio() const { return glide_speed / glide_sink; }
};

struct World {
    float cell_size = 64;
    int cells_x = 1, cells_z = 1;
    base::NavPoint spawn{8, 0, 8};
    Traversal rules;
    std::vector<Cell> cells;
    std::vector<Poi> pois;
    std::vector<std::string> items, flags;
    std::vector<Ability> abilities;
    std::vector<Mission> missions;
    moira::Ref<moira::Array> tree;  // keeps the goal commands alive

    [[nodiscard]] int poi(const std::string& name) const;
    [[nodiscard]] int item(const std::string& name) const;
    [[nodiscard]] int flag(const std::string& name) const;
    [[nodiscard]] int mission(const std::string& name) const;
    [[nodiscard]] int cell_at(float x, float z) const;  // -1 outside
    // The navigation flags the holder of `items` (a bit per item) may use.
    [[nodiscard]] uint16_t abilities_of(uint64_t items) const;
};

// Reads (wayfarer ...), (world ...), (items ...), (abilities ...), (cells ...), (pois ...),
// and every (missions ...) (one per region file). False, with the reason, on a name that is
// not defined or a world too large for the analyses (64 items, flags, or missions).
[[nodiscard]] bool load(const moira::Ref<moira::Array>& root, World& out, std::string& error);

// The navigation geometry of cell `c` and what overlaps its tile: its blocks, a ground slab,
// the climb links of its climbable blocks (up to climb_max), its drops, and its glides that
// the glide ratio allows. `skipped` counts glides too long to make.
[[nodiscard]] base::NavGeometry geometry(const World& w, int c, int* skipped = nullptr);
// Every cell's tile, for the analyses (the game streams them instead).
[[nodiscard]] bool build_nav(const World& w, base::NavMesh& nav, std::string* error = nullptr);
[[nodiscard]] base::NavSettings nav_settings(const World& w);
// What the navigation geometry depends on (every cell's source, the cell size, climb_max,
// step, and the glide ratio), hashed: a mesh built for one key serves until it changes.
[[nodiscard]] uint64_t nav_key(const World& w);

// --- Progress: what the director tracks, and the analyses search over ---

struct Progress {
    uint64_t items = 0, flags = 0, missions = 0;  // bits
    uint64_t started = 0;  // missions opened by hand ({mission.start}), whatever unlocks them
    std::vector<uint64_t> objectives;  // per mission, a bit per objective
    std::vector<bool> found;           // per poi: discovered

    void reset(const World& w);
    [[nodiscard]] bool has(int item) const { return item >= 0 && (items >> item & 1); }
    [[nodiscard]] bool done(int mission) const { return mission >= 0 && (missions >> mission & 1); }
    [[nodiscard]] bool unlocked(const World& w, int mission) const;
    // Whether objective o of mission m is open: unlocked, not done, its requires done.
    [[nodiscard]] bool open(const World& w, int m, int o) const;
};

// A goal's truth with the player at `place` (a poi, or -1 elsewhere).
[[nodiscard]] bool holds(const World& w, const Progress& p, const Goal& g, int place);
// Visiting poi `at`: its items and flag, if its needs are held. Then every open objective
// whose goal holds is done, and every mission whose objectives are all done gives its
// reward, until nothing changes. Returns the objectives done.
int visit(const World& w, Progress& p, int at);
// Only the visit's own effects (found, items, flag): the game's director then evaluates the
// goals as written.
void take(const World& w, Progress& p, int at);
int settle(const World& w, Progress& p, int place);

// --- The analyses (§4.4, "Agent loop") ---

struct Softlock {
    std::string where;  // the place, and what is done there
    std::string why;    // the missions that can no longer be finished
};
struct Proof {
    int states = 0;                   // save points searched
    std::vector<Softlock> softlocks;  // the first few
    int softlocked_states = 0;
    std::vector<std::string> unreachable;       // "mission.objective", never done in any state
    std::vector<std::string> unreachable_pois;  // points no save point is at
    std::vector<std::string> unanalysed;        // goals the search cannot read
    bool complete = false;                      // every mission can be finished from the start
};
// Searches every state the player can save in (a place, and what is held, set, and done),
// moving between the spawn and the points of interest by paths on `nav` with the abilities
// held, and proves every mission completable from each, or reports where it is not.
[[nodiscard]] Proof prove(const World& w, const base::NavMesh& nav);

enum class Style : uint8_t { Direct, Curious, Completionist };
[[nodiscard]] bool parse_style(const std::string& s, Style& out);

struct Pacing {
    std::vector<float> discoveries;   // seconds, of each poi found
    float longest_gap = 0;            // seconds without a discovery, start and end included
    float longest_gap_at = 0;         // when that stretch began
    std::vector<float> mission_done;  // per mission, seconds; -1 not done
    float all_done = -1;
    int found = 0;
    std::vector<int> missed;  // the points it never found
};
// A bot of `style` explores for `seconds` at the run speed, on `nav`, with the abilities its
// items give it: direct follows the open objectives; curious heads for the nearest point in
// sight it has not found, else follows an objective, else wanders to one it has not found;
// completionist visits every point nearest first, then the objectives. It stops when it has
// nothing left to do that it can reach. Seeded.
[[nodiscard]] Pacing explore(const World& w, const base::NavMesh& nav, Style style, float seconds,
                             uint64_t seed);

// The glides in the world: each one's ratio margin (the glide ratio over the ratio it
// needs; below 1 it cannot be made).
[[nodiscard]] std::vector<float> glide_margins(const World& w);

}  // namespace wayfarer
