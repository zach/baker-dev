#include "fight.hpp"

#include <baker/parse.hpp>

#include <doctest.h>

#include <string>

namespace {

const char* kData = R"(
(fighters
   (kestrel (health 1000) (width 0.6) (height 1.8) (crouch_height 1.1)
      (walk_forward 2.4) (walk_back 1.8)
      (moves
         (jab  (button light) (startup 4) (active 2) (recovery 7) (damage 30) (hitstun 12)
               (blockstun 9) (push 0.12) (cancels jab low) (hitbox 0.3 1.3 0.55 0.2))
         (low  (button kick) (crouching 1) (startup 6) (active 3) (recovery 12) (damage 45)
               (hitstun 16) (blockstun 10) (push 0.15) (guard low) (hitbox 0.3 0.1 0.6 0.25))
         (big  (button heavy) (startup 9) (active 3) (recovery 18) (damage 90) (hitstun 20)
               (blockstun 14) (push 0.25) (hitbox 0.35 1.1 0.7 0.3)))))
(cpu (normal (reaction 20) (block 0.5) (aggression 0.5) (combo 0.5) (approach 0.6) (decide_every 6)))
(bots (masher (reaction 40) (mash 0.2) (approach 0.7)))
)";

duel::Roster roster(const char* text = kData) {
    auto tree = baker::read_string(text);
    REQUIRE(tree);
    duel::Roster r;
    std::string error;
    REQUIRE_MESSAGE(duel::load(**tree, r, error), error);
    return r;
}

// Two kestrels, touching, the left one attacking.
duel::World close_world(const duel::Roster& r, const duel::Rules& rules) {
    duel::World w;
    duel::start_round(w, r, rules);
    w.f[0].x = -300;
    w.f[1].x = 300;
    return w;
}

void run(duel::World& w, const duel::Roster& r, const duel::Rules& rules, duel::Input a,
         duel::Input b = 0, int frames = 1) {
    for (int i = 0; i < frames; ++i) {
        const duel::Input in[2] = {a, b};
        duel::step(w, r, rules, in);
    }
}

bool actionable(const duel::Fighter& f) {
    return f.state == duel::State::Idle || f.state == duel::State::Walk ||
           f.state == duel::State::Crouch;
}

}  // namespace

TEST_CASE("duel: the data converts to integers once, at load") {
    const duel::Roster r = roster();
    REQUIRE(r.fighters.size() == 1);
    const duel::FighterData& k = r.fighters[0];
    CHECK(k.width == 600);
    CHECK(k.walk_forward == 40);  // 2.4 m/s at 60 frames a second
    REQUIRE(k.moves.size() == 3);
    CHECK(k.moves[0].hitbox.x == 300);
    CHECK(k.moves[0].cancels == 0b011u);  // jab and low
    CHECK(k.moves[1].guard == duel::Guard::Low);
    CHECK(k.moves[1].crouching);
    CHECK(r.find_brain("normal")->block == 500);
    CHECK(r.find_brain("masher")->mash == 200);

    duel::Roster bad;
    std::string error;
    auto tree = baker::read_string(
        "(fighters (k (moves (a (button light) (startup 1) (active 1) (recovery 1) "
        "(cancels nope) (hitbox 0 0 1 1)))))");
    REQUIRE(tree);
    CHECK_FALSE(duel::load(**tree, bad, error));
    CHECK(error.find("nope") != std::string::npos);
}

TEST_CASE("duel: a hit's advantage in the fight is the frame table's") {
    const duel::Roster r = roster();
    duel::Rules rules;
    for (size_t m : {size_t(0), size_t(2)}) {  // jab, big
        const duel::Move& move = r.fighters[0].moves[m];
        duel::World w = close_world(r, rules);
        run(w, r, rules, move.button);
        int frames = 1;
        while (w.last.kind != duel::Event::Hit && frames < 100) run(w, r, rules, 0), ++frames;
        REQUIRE(w.last.kind == duel::Event::Hit);
        CHECK(frames == move.startup + 1);
        CHECK(w.last.advantage == move.on_hit());
        CHECK(w.f[1].health == 1000 - move.damage);
        // Count, after the hit pause, the frames until each side can act.
        while (w.pause > 0) run(w, r, rules, 0);
        int attacker = -1, defender = -1;
        for (int t = 1; t < 100 && (attacker < 0 || defender < 0); ++t) {
            run(w, r, rules, 0);
            if (attacker < 0 && actionable(w.f[0])) attacker = t;
            if (defender < 0 && actionable(w.f[1])) defender = t;
        }
        CHECK(defender - attacker == move.on_hit());
    }
}

TEST_CASE("duel: back blocks a mid; a low must be blocked crouching") {
    const duel::Roster r = roster();
    duel::Rules rules;
    duel::World w = close_world(r, rules);
    run(w, r, rules, duel::kLight, duel::kRight, 1);  // right side's back is to the right
    run(w, r, rules, 0, duel::kRight, 6);
    CHECK(w.f[1].state == duel::State::Blockstun);
    CHECK(w.f[1].health == 1000);

    w = close_world(r, rules);
    run(w, r, rules, duel::kKick | duel::kDown, duel::kRight, 1);
    run(w, r, rules, duel::kDown, duel::kRight, 8);
    CHECK(w.f[1].health == 1000 - 45);  // standing guard: the low hits

    w = close_world(r, rules);
    run(w, r, rules, duel::kKick | duel::kDown, duel::kRight | duel::kDown, 1);
    run(w, r, rules, duel::kDown, duel::kRight | duel::kDown, 8);
    CHECK(w.f[1].health == 1000);
}

TEST_CASE("duel: at a wall, pushback moves the attacker instead") {
    const duel::Roster r = roster();
    duel::Rules rules;
    duel::World w;
    duel::start_round(w, r, rules);
    w.f[1].x = rules.half_width - 300;  // against the right wall
    w.f[0].x = w.f[1].x - 600;
    const int32_t before = w.f[0].x;
    run(w, r, rules, duel::kLight);
    run(w, r, rules, 0, 0, 5);
    CHECK(w.f[1].x == rules.half_width - 300);
    CHECK(w.f[0].x == before - 120);
}

TEST_CASE("duel: two pilots with the same seeds fight the same fight") {
    const duel::Roster r = roster();
    duel::Rules rules;
    auto fight = [&](uint64_t seed) {
        duel::World w;
        duel::start_round(w, r, rules);
        duel::Pilot p[2];
        p[0].reset(r.find_brain("masher"), seed * 2);
        p[1].reset(r.find_brain("normal"), seed * 2 + 1);
        for (int i = 0; i < 60 * 120; ++i) {
            const duel::Input in[2] = {p[0].think(w, r, 0), p[1].think(w, r, 1)};
            duel::step(w, r, rules, in);
        }
        return std::to_string(w.rounds[0]) + ":" + std::to_string(w.rounds[1]) + ":" +
               std::to_string(w.f[0].health) + ":" + std::to_string(w.f[1].health) + ":" +
               std::to_string(w.f[0].x) + ":" + std::to_string(p[1].fastest_reaction());
    };
    CHECK(fight(5) == fight(5));
    CHECK(fight(5) != fight(6));
    // The CPU never answers faster than it can see.
    duel::World w;
    duel::start_round(w, r, rules);
    duel::Pilot cpu, bot;
    cpu.reset(r.find_brain("normal"), 3);
    bot.reset(r.find_brain("masher"), 4);
    for (int i = 0; i < 60 * 300; ++i) {
        const duel::Input in[2] = {bot.think(w, r, 0), cpu.think(w, r, 1)};
        duel::step(w, r, rules, in);
    }
    CHECK(cpu.fastest_reaction() >= 20);
    CHECK(w.rounds[0] + w.rounds[1] + w.draws > 0);
}

TEST_CASE("duel: the combo search finds chains, and the loop that never ends") {
    const duel::Roster r = roster();
    duel::Rules rules;
    const duel::ComboReport c = duel::combo_search(r, rules);
    CHECK_FALSE(c.infinite);
    CHECK(c.max_hits >= 3);  // jab > jab > ... > low
    CHECK(c.max_damage >= 30 * 2 + 45);
    CHECK(c.route.find("low") != std::string::npos);

    // No pushback and no hitstun decay: jab cancels into jab forever.
    std::string text = kData;
    for (const char* push : {"(push 0.12)", "(push 0.15)", "(push 0.25)"})
        text.replace(text.find(push), std::string(push).size(), "(push 0)");
    const duel::Roster loop = roster(text.c_str());
    rules.stun_decay = 0;
    const duel::ComboReport inf = duel::combo_search(loop, rules, 30);
    CHECK(inf.infinite);
    CHECK(inf.infinite_route.find("jab > jab > jab") != std::string::npos);
}

TEST_CASE("duel: frame table") {
    const auto rows = duel::frame_table(roster());
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].on_hit == 4);    // 12 + 1 - 2 - 7
    CHECK(rows[0].on_block == 1);  // 9 + 1 - 2 - 7
    CHECK(rows[2].on_block == -6);
}
