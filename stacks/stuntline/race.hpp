// Stuntline's races (game-stacks.md §4.5): tracks laid end to end from a kit of pieces, a
// car on Jolt's wheeled vehicle, AI drivers that follow the line, and what the agent
// measures: validity, finishing time, crashes by piece, airtime, and whether each jump needs
// commitment. No window here.
//
// A race is one Jolt world (base::Physics): the road, then the car, stepped at a fixed rate;
// with the same driver and seed it replays bit for bit (§3.1).
#pragma once

#include <base/physics.hpp>

#include <moira/array.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace stuntline {

enum class Kind : uint8_t { Straight, Ramp, Gap, Turn };

// A piece of the kit, from (pieces ...).
struct PieceDef {
    std::string name;
    Kind kind = Kind::Straight;
    float length = 20;    // metres, horizontal (straight, ramp, gap)
    float rise = 0;       // metres (ramp)
    float radius = 30;    // metres (turn)
    float angle = 90;     // degrees, positive to the left (turn)
    float bank = 0;       // degrees, toward the inside (turn)
    float width = 12;     // metres
    std::string in, out;  // connector types: a piece's in must match the one before's out
};

struct Track {
    std::string name, desc;
    std::vector<int> lay;  // pieces, in order
    float par = 60;        // seconds: a good time
};

// An AI driver, from (drivers ...).
struct Driver {
    std::string name;
    float pace = 0.9f;      // share of each corner's grip-limited speed it takes
    float noise = 0.0f;     // steering noise, as a share of full lock
    float look_ahead = 12;  // metres along the line it steers toward
    float top_speed = 45;   // m/s it will not exceed
};

struct Car {
    float mass = 1500;
    float half_width = 0.9f, half_height = 0.2f, half_length = 2.0f;  // metres
    float wheel_radius = 0.3f, wheel_width = 0.1f;
    float max_torque = 500;  // N m
    float max_rpm = 6000;
    float max_steer = 30;  // degrees
    float suspension_min = 0.3f, suspension_max = 0.5f;
    float suspension_frequency = 1.5f, suspension_damping = 0.5f;
    float grip = 1.0f;  // the friction a corner's speed is planned with
};

struct Kit {
    std::vector<PieceDef> pieces;
    std::vector<Track> tracks;
    std::vector<Driver> drivers;
    Car car;

    [[nodiscard]] int piece(const std::string& name) const;
    [[nodiscard]] int track(const std::string& name) const;
    [[nodiscard]] int driver(const std::string& name) const;
};

// Reads (car ...), (pieces ...), (tracks ...), and (drivers ...). False, with the reason, on
// a track naming a piece the kit does not have.
[[nodiscard]] bool load(const moira::Array& root, Kit& out, std::string& error);

// A box of road: center, half extents (width, thickness, length), and orientation.
struct Segment {
    float center[3];
    float half[3];
    float quat[4];  // x y z w
    int piece;      // index into the track's lay
};

// A point on the racing line, every metre.
struct Sample {
    float pos[3];       // on the road's surface
    float yaw;          // heading, radians; 0 is +z, positive turns left (toward +x)
    float s;            // metres from the start
    float speed_limit;  // m/s a car can take here at full grip; large on straights
    int piece;
    bool gap;
};

struct Layout {
    std::vector<int> lay;  // the track's pieces, by kit index
    std::vector<Segment> road;
    std::vector<Sample> line;
    float length = 0;  // of the line, metres
    float lowest = 0;  // the lowest road surface: below it by 5 m is a fall
    bool connected = true;
    std::string problem;  // the first connector that does not match
};
[[nodiscard]] Layout lay_out(const Kit& kit, const Track& track);

// What drives the car in a step: throttle 0..1, steering -1 (left) .. 1 (right), brake 0..1.
struct Controls {
    float forward = 0, right = 0, brake = 0, handbrake = 0;
};

// One race: the road and the car in a Jolt world.
class Race {
public:
    Race(const Kit& kit, const Layout& layout);
    ~Race();
    Race(const Race&) = delete;
    Race& operator=(const Race&) = delete;

    static constexpr float kDt = 1.0f / 60.0f;
    void step(const Controls& c);

    [[nodiscard]] base::Physics& physics() noexcept { return world_; }
    [[nodiscard]] const Layout& layout() const noexcept { return layout_; }
    // The car's body: position, orientation (x y z w), and speed along its heading (m/s).
    void body(float pos[3], float quat[4]) const;
    // Wheel i (0..3): position, orientation.
    void wheel(int i, float pos[3], float quat[4]) const;
    [[nodiscard]] float speed() const noexcept { return speed_; }
    [[nodiscard]] float progress() const noexcept { return s_; }  // metres along the line
    [[nodiscard]] int sample() const noexcept { return sample_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] float seconds() const noexcept { return float(steps_) * kDt; }
    [[nodiscard]] bool airborne() const noexcept { return airborne_; }

    // What the race has seen so far.
    int crashes = 0;
    std::vector<int> crash_pieces;  // the lay index of each crash
    float airtime = 0;              // seconds with no wheel touching
    float top_speed = 0;
    std::vector<float> gap_arrival;  // speed at the start of each gap, the first time

    // Puts the car back, still, five metres into the last flat piece at or before `piece`.
    void respawn(int piece);

private:
    void place(const Sample& at);

    const Kit& kit_;
    Layout layout_;
    base::Physics world_;
    struct Vehicle;
    std::unique_ptr<Vehicle> vehicle_;
    int steps_ = 0, sample_ = 0;
    float s_ = 0, speed_ = 0;
    bool finished_ = false, airborne_ = false;
    int upside_down_ = 0, stuck_ = 0, grace_ = 0;
    int last_gap_seen_ = -1;
};

// The controls an AI driver gives this step. `rng` is its own random state.
[[nodiscard]] Controls drive(const Driver& d, const Kit& kit, const Race& race, uint64_t& rng);

struct Result {
    bool finished = false;
    float seconds = 0;
    int crashes = 0;
    std::vector<int> crash_pieces;
    float airtime = 0;
    float top_speed = 0;
    std::vector<float> gap_arrival;
    uint64_t hash = 0;  // the world's state at the end
};
// Runs a whole race with an AI driver, up to `max_seconds`. Seeded.
[[nodiscard]] Result race(const Kit& kit, const Layout& layout, const Driver& d, uint64_t seed,
                          float max_seconds);

// Each gap's horizontal flight at `speed` off the lip before it (ballistic, from the lip's
// angle), over the gap's length: above 1 clears it.
[[nodiscard]] std::vector<float> gap_margins(const Kit& kit, const Track& track,
                                             const std::vector<float>& speeds);

}  // namespace stuntline
