// Duel's simulation (game-stacks.md §4.6): two fighters, frame data, boxes against boxes, a
// CPU opponent, and the analyses the agent runs over the data: frame tables and a combo
// search. No window and no Baker here beyond reading the data, so the headless runner, the
// game, and the tests step the same code.
//
// Everything is an integer: time in frames, distance in millimetres, chances in per mille.
// The data's floats (metres, fractions) are converted once, at load, so a fight is the same
// on any compiler and CPU (§7), and a replay is its inputs.
#pragma once

#include <baker/array.hpp>
#include <base/rng.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace duel {

// One frame's input, as held buttons; directions are the screen's, not the fighter's.
enum Button : uint8_t {
    kLeft = 1,
    kRight = 2,
    kDown = 4,
    kLight = 8,
    kHeavy = 16,
    kKick = 32,
    kSpecial = 64,
};
using Input = uint8_t;
constexpr Input kAttackButtons = kLight | kHeavy | kKick | kSpecial;

// How a move must be blocked: mid by either guard, low crouching, overhead standing.
enum class Guard : uint8_t { Mid, Low, Overhead };

struct Rect {
    int32_t x = 0, y = 0, w = 0, h = 0;  // mm: x forward of the fighter's center, y up
};

struct Move {
    std::string name;
    Input button = kLight;                      // the button that starts it
    bool crouching = false;                     // with down held
    int startup = 1, active = 1, recovery = 1;  // frames; active from frame startup + 1
    int damage = 0, hitstun = 0, blockstun = 0;
    int32_t push = 0;  // mm the defender slides away on hit or block; at a wall, the attacker
    Guard guard = Guard::Mid;
    Rect hitbox;
    uint32_t cancels = 0;  // a bit per move of the same fighter it may cancel into

    [[nodiscard]] int total() const noexcept { return startup + active + recovery; }
    // Frames the attacker is ahead when the move connects on its first active frame.
    [[nodiscard]] int on_hit() const noexcept { return hitstun + 1 - active - recovery; }
    [[nodiscard]] int on_block() const noexcept { return blockstun + 1 - active - recovery; }
};

struct FighterData {
    std::string name;
    int health = 1000;
    int32_t width = 600, height = 1800, crouch_height = 1100;  // mm
    int32_t walk_forward = 40, walk_back = 30;                 // mm per frame
    std::vector<Move> moves;                                   // at most 32
};

// A decision maker: the CPU at a difficulty, or a scripted player bot (masher, turtle,
// combo). The same few numbers describe both.
struct Brain {
    std::string name;
    int reaction = 20;     // frames before it sees what the opponent does
    int block = 0;         // per mille, per decision near the opponent: guards (holds back),
                           // crouching only for a low it sees
    int punish = 0;        // per mille: attacks a move it sees recovering, in range
    int aggression = 0;    // per mille, per decision: attacks when in range
    int combo = 0;         // per mille: cancels a move that connected
    int mash = 0;          // per mille, per frame: presses a random button, in range or not
    int approach = 500;    // per mille, per decision: walks in rather than back
    int decide_every = 6;  // frames between decisions
};

struct Rules {
    int hit_pause = 6;           // frames both fighters freeze when a move connects
    int buffer = 5;              // frames a press waits for its fighter to be able to act
    int stun_decay = 1;          // frames of hitstun each hit of a combo loses
    int32_t half_width = 4000;   // mm from the stage's center to a wall
    int32_t start_gap = 2000;    // mm between the fighters at a round's start
    int round_frames = 60 * 60;  // a round's time limit
    int ko_frames = 90;          // frames between a knockout and the next round
};

struct Roster {
    std::vector<FighterData> fighters;
    std::vector<Brain> cpu;   // difficulties: easy, normal, hard, ...
    std::vector<Brain> bots;  // player bots for the simulation

    [[nodiscard]] int fighter(const std::string& name) const;
    [[nodiscard]] const Brain* find_brain(const std::string& name) const;
};

// Reads (fighters ...), (cpu ...), and (bots ...) from the merged data. False, with the
// reason, when a fighter has no moves or more than 32, or a cancel names no move.
[[nodiscard]] bool load(const baker::Array& root, Roster& out, std::string& error);

enum class State : uint8_t { Idle, Walk, Crouch, Attack, Hitstun, Blockstun, Down };

struct Fighter {
    int32_t x = 0;
    int8_t facing = 1;  // +1: faces +x
    int health = 0;
    State state = State::Idle;
    int timer = 0;       // stun frames left
    int move = -1;       // the move under way, by index
    int move_frame = 0;  // 1 on the move's first frame
    bool connected = false, blocking = false, crouching = false;
    Input held = 0, previous = 0;
    int buffered = -1, buffer_left = 0;    // a pressed move waiting to start
    int combo_hits = 0, combo_damage = 0;  // the combo this fighter is taking
    int attack_frame = 0;                  // the frame the current move started (for reactions)
};

// What happened in a step, for the HUD, the training panel, and the statistics.
struct Event {
    enum Kind : uint8_t { None, Hit, Block, Ko, TimeUp } kind = None;
    int attacker = -1;
    int advantage = 0;  // frames the attacker is ahead, from this contact
};

struct World {
    Fighter f[2];
    int fighter_data[2] = {0, 0};  // indices into Roster::fighters
    int frame = 0;                 // since the match started
    int round_frame = 0;           // since the round started
    int pause = 0;                 // hit pause frames left
    int ko = 0;                    // frames left before the next round; 0: fighting
    int rounds[2] = {0, 0};        // rounds won
    int draws = 0;
    Event last{};
};

// A new round: fighters at their marks, full health; round counts kept.
void start_round(World& w, const Roster& roster, const Rules& rules);
// One frame, with each fighter's held buttons.
void step(World& w, const Roster& roster, const Rules& rules, const Input input[2]);
// The fighter's hurtbox and active hitbox in world space (mm, x and y of the lower left
// corner); false when no hitbox is active.
[[nodiscard]] Rect hurtbox(const Fighter& f, const FighterData& d);
[[nodiscard]] bool hitbox(const Fighter& f, const FighterData& d, Rect& out);

// The CPU or a bot, playing one side. It sees the world as it was `reaction` frames ago.
class Pilot {
public:
    void reset(const Brain* brain, uint64_t seed);
    [[nodiscard]] const Brain* brain() const noexcept { return brain_; }
    // The buttons to hold this frame, as side `me`.
    [[nodiscard]] Input think(const World& w, const Roster& roster, int me);
    // The fewest frames it took to change its guard for an attack it saw (crouching for a
    // low, standing for anything else), since reset; -1 if it never did.
    [[nodiscard]] int fastest_reaction() const noexcept { return fastest_; }

private:
    [[nodiscard]] uint32_t roll() noexcept;  // 0..999
    [[nodiscard]] bool chance(int per_mille) noexcept { return int(roll()) < per_mille; }

    const Brain* brain_ = nullptr;
    base::Rng rng_;
    static constexpr int kHistory = 64;
    Fighter seen_[kHistory];  // the opponent, by frame, for reaction delay
    int seen_count_ = 0;
    Input plan_ = 0;  // held between decisions
    int next_decision_ = 0;
    int answered_attack_ = -1;  // the attack (by its start frame) already guarded against
    int punished_ = -1;         // the attack (by its start frame) already answered
    int blocked_move_ = -1;     // the opponent's move it is blocking, to punish after
    bool guarding_ = false, guard_low_ = false;
    int fastest_ = -1;
};

// The analyses (§4.6, "Agent loop").
struct FrameRow {
    std::string fighter, move;
    int startup, active, recovery, damage, on_hit, on_block;
};
[[nodiscard]] std::vector<FrameRow> frame_table(const Roster& roster);

struct ComboReport {
    int max_damage = 0;
    int max_hits = 0;
    bool infinite = false;  // a combo reached the search's hit cap
    std::string route;      // the most damaging route: "jab > jab > low_kick"
    std::string infinite_route;
    int searched = 0;  // states explored
};
// Searches every fighter's combos against an opponent of the same kind, from touching
// range, in mid-stage and in the corner: each opener, then every cancel and every link
// (walking in 0 or more frames first) that hits before the opponent recovers.
[[nodiscard]] ComboReport combo_search(const Roster& roster, const Rules& rules, int max_hits = 40);

}  // namespace duel
