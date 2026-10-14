#include "world.hpp"

#include <moira/config.hpp>
#include <moira/parse.hpp>
#include <moira/std_interop.hpp>

#include <doctest.h>

#include <string>

namespace {

// One cell: a walled yard (x 40..56, z 40..56) with a relic in it, entered only by dropping
// from a platform a climber can get onto; gloves for sale outside, and a tower to visit.
// Mission `relic` is in the yard; mission `tower` is outside. Do the relic first and you are
// shut in the yard with the tower undone: a softlock, though everything can be finished
// from the start.
const char* kYard = R"(
(world (cell_size 64) (spawn 10 0 10))
(items gloves)
(abilities (climb (item gloves)))
(cells
   (only (grid 0 0)
      (blocks (48 40 17 1 4) (48 56 17 1 4) (40 48 1 15 4) (56 48 1 15 4))
      (climbable (36 48 6 8 4))
      (drops ((38.5 4 48) (44 0 48)))))
(pois
   (shop (pos 10 0 30) (gives gloves))
   (relic (pos 48 0 48))
   (tower (pos 20 0 55)))
(missions
   (relic (region yard) (objectives (find (goal {at_poi relic}))))
   (tower (region yard) (objectives (visit (goal {at_poi tower})))))
)";

wayfarer::World world(const std::string& text) {
    auto tree = moira::read_string(text);
    REQUIRE(tree);
    wayfarer::World w;
    std::string error;
    REQUIRE_MESSAGE(wayfarer::load(*tree, w, error), error);
    return w;
}

std::string replace(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("wayfarer: the shipped world proves: no softlocks, everything reachable") {
    moira::Config cfg;
    cfg.session().set_pack_levels({"engine", "game", "task"});
    cfg.session().add_include_dir(WAYFARER_DATA);
    auto pushed = cfg.push_layer("default", WAYFARER_DATA "/default.dta");
    REQUIRE_MESSAGE(pushed, (pushed ? "" : std::string(moira::to_string(pushed.error()))));
    wayfarer::World w;
    std::string error;
    REQUIRE_MESSAGE(wayfarer::load(cfg.root(), w, error), error);
    CHECK(w.cells.size() == 9);
    CHECK(w.missions.size() == 6);
    CHECK(w.missions[0].file.find("south_coast.dta") != std::string::npos);
    base::NavMesh nav;
    REQUIRE_MESSAGE(wayfarer::build_nav(w, nav, &error), error);
    CHECK(nav.tiles() == 9);
    const wayfarer::Proof p = wayfarer::prove(w, nav);
    CHECK(p.complete);
    CHECK(p.softlocked_states == 0);
    CHECK(p.unreachable.empty());
    CHECK(p.unreachable_pois.empty());
    CHECK(p.unanalysed.empty());
    CHECK(p.states > 100);
}

TEST_CASE("wayfarer: a one-way drop into a yard is a softlock if a mission is left outside") {
    const wayfarer::World w = world(kYard);
    base::NavMesh nav;
    std::string error;
    REQUIRE_MESSAGE(wayfarer::build_nav(w, nav, &error), error);
    const wayfarer::Proof p = wayfarer::prove(w, nav);
    CHECK(p.complete);  // tower first, then the relic, finishes everything
    CHECK(p.softlocked_states > 0);
    REQUIRE(!p.softlocks.empty());
    CHECK(p.softlocks[0].where.find("at relic") != std::string::npos);
    CHECK(p.softlocks[0].why.find("tower") != std::string::npos);

    // A way back out (a crate by the east wall to climb, and a drop beyond) ends it.
    const wayfarer::World fixed =
        world(replace(replace(kYard, "(drops ((38.5 4 48) (44 0 48)))",
                              "(drops ((38.5 4 48) (44 0 48)) ((54 4 48) (60 0 48)))"),
                      "(climbable (36 48 6 8 4))", "(climbable (36 48 6 8 4) (53.5 48 4 6 4))"));
    base::NavMesh nav2;
    REQUIRE(wayfarer::build_nav(fixed, nav2, &error));
    const wayfarer::Proof q = wayfarer::prove(fixed, nav2);
    CHECK(q.softlocked_states == 0);
    CHECK(q.complete);
}

TEST_CASE("wayfarer: without the gloves the relic is unreachable, and the proof says so") {
    const wayfarer::World w =
        world(replace(kYard, "(shop (pos 10 0 30) (gives gloves))", "(shop (pos 10 0 30))"));
    base::NavMesh nav;
    std::string error;
    REQUIRE(wayfarer::build_nav(w, nav, &error));
    const wayfarer::Proof p = wayfarer::prove(w, nav);
    CHECK_FALSE(p.complete);
    REQUIRE(p.unreachable.size() == 1);
    CHECK(p.unreachable[0] == "relic.find");
    REQUIRE(p.unreachable_pois.size() == 1);
    CHECK(p.unreachable_pois[0] == "relic");
}

TEST_CASE("wayfarer: a goal the proof cannot read is reported, not guessed") {
    const wayfarer::World w = world(
        replace(kYard, "(visit (goal {at_poi tower}))", "(visit (goal {> {time_of_day} 20}))"));
    base::NavMesh nav;
    std::string error;
    REQUIRE(wayfarer::build_nav(w, nav, &error));
    const wayfarer::Proof p = wayfarer::prove(w, nav);
    REQUIRE(p.unanalysed.size() == 1);
    CHECK(p.unanalysed[0] == "tower.visit");
    CHECK_FALSE(p.complete);
}

TEST_CASE("wayfarer: a glide is made only within the glide ratio") {
    // A ledge 10 m up, and a glide 40 m out: a ratio of 4 needed.
    std::string text = replace(kYard, "(drops ((38.5 4 48) (44 0 48)))",
                               "(drops ((38.5 4 48) (44 0 48)))\n"
                               "(blocks (10 50 6 6 10))\n(glides ((10 10 47) (10 0 7)))");
    text = replace(text, "(world (cell_size 64)",
                   "(wayfarer (glide_speed 10) (glide_sink 2))\n(world (cell_size 64)");
    wayfarer::World w = world(text);
    auto margins = wayfarer::glide_margins(w);
    REQUIRE(margins.size() == 1);
    CHECK(margins[0] == doctest::Approx(5.0f / 4.0f));
    int skipped = 0;
    (void)wayfarer::geometry(w, 0, &skipped);
    CHECK(skipped == 0);
    w.rules.glide_sink = 3;  // a ratio of 3.3: too short
    (void)wayfarer::geometry(w, 0, &skipped);
    CHECK(skipped == 1);
}

TEST_CASE("wayfarer: bots explore; the direct one finishes the missions, in order") {
    const wayfarer::World w = world(kYard);
    base::NavMesh nav;
    std::string error;
    REQUIRE(wayfarer::build_nav(w, nav, &error));
    const wayfarer::Pacing d = wayfarer::explore(w, nav, wayfarer::Style::Direct, 600, 1);
    REQUIRE(d.mission_done.size() == 2);
    CHECK(d.mission_done[0] >= 0);
    CHECK(d.all_done > 0);
    const wayfarer::Pacing c = wayfarer::explore(w, nav, wayfarer::Style::Completionist, 600, 1);
    CHECK(c.missed.empty());
    CHECK(c.found == 3);
    CHECK(c.longest_gap > 0);
}
