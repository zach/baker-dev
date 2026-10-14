#include "race.hpp"

#include <moira/parse.hpp>

#include <doctest.h>

#include <cmath>
#include <string>

namespace {

const char* kData = R"(
(car (mass 1500) (max_torque 900))
(pieces
   (straight_40 (kind straight) (length 40) (in flat) (out flat))
   (kicker_15   (kind ramp) (length 15) (rise 3) (in flat) (out lip))
   (gap_8       (kind gap) (length 8) (in lip) (out lip))
   (landing_20  (kind ramp) (length 20) (rise -3) (in lip) (out flat))
   (left_90     (kind turn) (radius 30) (angle 90) (in flat) (out flat)))
(tracks
   (hop (lay straight_40 straight_40 kicker_15 gap_8 landing_20 straight_40) (par 20))
   (bend (lay straight_40 left_90 straight_40) (par 20))
   (broken (lay straight_40 gap_8 straight_40) (par 20)))
(drivers (pro (pace 0.9) (noise 0.05) (look_ahead 12) (top_speed 40)))
)";

stuntline::Kit kit() {
    auto tree = moira::read_string(kData);
    REQUIRE(tree);
    stuntline::Kit k;
    std::string error;
    REQUIRE_MESSAGE(stuntline::load(**tree, k, error), error);
    return k;
}

}  // namespace

TEST_CASE("stuntline: a track lays out end to end; connectors are checked") {
    const stuntline::Kit k = kit();
    const stuntline::Layout bend = stuntline::lay_out(k, k.tracks[1]);
    CHECK(bend.connected);
    // Two 40 m straights and a quarter circle of radius 30.
    CHECK(bend.length == doctest::Approx(80 + 30 * 3.14159f / 2).epsilon(0.03));
    // The bend turns the heading a quarter turn to the left (toward +x).
    CHECK(bend.line.back().yaw == doctest::Approx(3.14159f / 2).epsilon(0.01));
    CHECK(bend.line.back().pos[0] > 25);
    const stuntline::Layout broken = stuntline::lay_out(k, k.tracks[2]);
    CHECK_FALSE(broken.connected);
    CHECK(broken.problem.find("gap_8") != std::string::npos);
}

TEST_CASE("stuntline: the pro jumps the gap and finishes; a race replays bit for bit") {
    const stuntline::Kit k = kit();
    const stuntline::Layout hop = stuntline::lay_out(k, k.tracks[0]);
    const stuntline::Result a = stuntline::race(k, hop, k.drivers[0], 1, 60);
    CHECK(a.finished);
    CHECK(a.crashes == 0);
    REQUIRE(a.gap_arrival.size() == 1);
    CHECK(a.gap_arrival[0] > 10);
    CHECK(a.airtime > 0.3f);
    const stuntline::Result b = stuntline::race(k, hop, k.drivers[0], 1, 60);
    CHECK(a.hash == b.hash);
    CHECK(a.seconds == b.seconds);
    // Computed on Linux with GCC; the MSVC job checks it on Windows (§4.5: ghosts must match
    // between the runner and the game).
    MESSAGE("hop race hash: ", a.hash);
    CHECK(a.hash == 8767161480715014559ull);
}

TEST_CASE("stuntline: a gap's margin is ballistic flight over its length") {
    const stuntline::Kit k = kit();
    const auto m = stuntline::gap_margins(k, k.tracks[0], {20.0f});
    REQUIRE(m.size() == 1);
    const float launch = std::atan2(3.0f, 15.0f);
    CHECK(m[0] == doctest::Approx(400 * std::sin(2 * launch) / 9.81f / 8));
}
