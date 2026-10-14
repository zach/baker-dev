#include "household.hpp"

#include <baker/parse.hpp>

#include <doctest.h>

#include <string>

namespace {

// Needs, things, a two-room house for two, and actions; each test changes what it needs.
const char* kData = R"(
(needs
   (hunger (decay_per_hour 6) (critical 20) (curve quadratic))
   (energy (decay_per_hour 4.5) (critical 15) (curve quadratic))
   (fun    (decay_per_hour 5) (critical 10) (curve linear)))
(objects
   (fridge (size 0.8 1.8 0.7) (advertise (hunger 35)) (minutes 15))
   (bed    (size 1.6 0.6 2.1) (advertise (energy 100)) (minutes 480) (users 2) (hours 21 7))
   (tv     (size 1.2 1 0.4) (advertise (fun 45)) (minutes 60) (users 2)))
(households
   (two_rooms
      (size 12 8)
      (walls (0 -4 12 0.2) (0 4 12 0.2) (-6 0 0.2 8) (6 0 0.2 8) (0 -2.5 0.2 3) (0 2.5 0.2 3))
      (things (fridge -5 -3 0) (tv -3 2 180) (bed 3 2.5 180))
      (residents (ada) (bo))))
)";

// The actions. Another file adds one with {extend villager ...}: a second {class} would
// replace them all.
const char* kUse = R"(
   (use_object ($obj)
      {clock thread_task {$this walk_to $obj} {wait {$this arrived}}
         {$this use $obj} {wait {$this used}} {$this done}}))";

std::string actions(const std::string& more = {}) {
    return "{class villager" + std::string(kUse) + more + "}";
}

hearth::Kit kit(const std::string& data = kData, const std::string& setup_text = actions()) {
    auto tree = baker::read_string(data);
    REQUIRE(tree);
    hearth::Kit k;
    std::string error;
    REQUIRE_MESSAGE(hearth::load(*tree, k, error), error);
    auto setup = baker::read_string(setup_text);
    REQUIRE(setup);
    for (const baker::Node& n : (*setup)->nodes()) k.setup.push_back(n);
    k.tree = *setup;  // keeps the setup alive; the kit copied what it reads from the data
    return k;
}

hearth::Tally soak(const hearth::Kit& k, int households, double days, int threads = 1) {
    hearth::SoakOptions o;
    o.households = households;
    o.days = days;
    o.threads = threads;
    const hearth::SoakResult r = hearth::soak(k, o);
    CHECK(r.errors.empty());
    return r.tally;
}

std::string replace(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    REQUIRE(at != std::string::npos);
    return s.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("hearth: a household lives days without getting stuck, oscillating, or starving") {
    const hearth::Kit k = kit();
    const hearth::Tally t = soak(k, 2, 5);
    CHECK(t.characters == 4);
    CHECK(t.character_days == doctest::Approx(20).epsilon(0.01));
    CHECK(t.stuck == 0);
    CHECK(t.oscillating == 0);
    CHECK(t.starving == 0);
    CHECK(t.script_errors == 0);
    CHECK(t.things_used == t.things);
    CHECK(t.decisions > 20);
    REQUIRE(t.entropy.size() == 4);
    for (double e : t.entropy) CHECK(e > 0);  // some give in the routine
}

TEST_CASE("hearth: a soak's results do not depend on the number of threads") {
    const hearth::Kit k = kit();
    const hearth::Tally one = soak(k, 6, 3, 1);
    const hearth::Tally three = soak(k, 6, 3, 3);
    CHECK(one.decisions == three.decisions);
    CHECK(one.interrupts == three.interrupts);
    CHECK(one.uses_by_kind == three.uses_by_kind);
    CHECK(one.entropy == three.entropy);
}

TEST_CASE("hearth: an action whose task never ends is reported stuck") {
    // The fridge's action waits for something that never happens.
    const hearth::Kit k = kit(replace(kData, "(advertise (hunger 35)) (minutes 15)",
                                      "(advertise (hunger 35)) (minutes 15) (action stare)"),
                              // From another file, as a task pack would: {extend}.
                              actions() + R"(
{extend villager
   (stare ($obj)
      {clock thread_task {$this walk_to $obj} {wait {$this arrived}} {wait {== 1 2}}})})");
    const hearth::Tally t = soak(k, 1, 2);
    CHECK(t.stuck > 0);
    CHECK(t.notes.size() > 0);
    CHECK(t.notes[0].find("stuck in stare") != std::string::npos);
}

TEST_CASE("hearth: a thing walled in is never walked to, and shows as unused") {
    // The fridge in a closed box of walls.
    const hearth::Kit k = kit(replace(kData, "(0 -2.5 0.2 3) (0 2.5 0.2 3))",
                                      "(0 -2.5 0.2 3) (0 2.5 0.2 3)\n"
                                      "(-5 -1.5 2.2 0.2) (-3.9 -2.75 0.2 2.5))"));
    const hearth::Tally t = soak(k, 1, 2);
    CHECK(t.things_used == t.things - 1);
    CHECK(t.uses_by_kind[0] == 0);
    CHECK(t.starving > 0);  // and with nothing else to eat, they starve
}

TEST_CASE("hearth: two needs that cut each other off are reported oscillating") {
    // Hunger and fun both fall fast and are critical early, and each use takes an hour: the
    // default reaction (interrupt unless the action helps) cuts eating off for fun and fun
    // off for eating.
    std::string data = replace(kData, "(hunger (decay_per_hour 6) (critical 20)",
                               "(hunger (decay_per_hour 40) (critical 60)");
    data = replace(data, "(fun    (decay_per_hour 5) (critical 10)",
                   "(fun    (decay_per_hour 40) (critical 60)");
    data = replace(data, "(advertise (hunger 35)) (minutes 15)",
                   "(advertise (hunger 100)) (minutes 60)");
    data = replace(data, "(advertise (fun 45)) (minutes 60)", "(advertise (fun 100)) (minutes 60)");
    // Only the two things, and always the best of them.
    data = replace(data, "(tv -3 2 180) (bed 3 2.5 180)", "(tv -3 2 180)");
    data += "(hearth (choice_top 1))";
    const hearth::Tally t = soak(kit(data), 1, 2);
    CHECK(t.interrupts > 10);
    CHECK(t.oscillating > 0);

    // A designer changes the reaction in script, without touching C++: carry on.
    const std::string steady =
        actions("\n   (on_need_critical ($need) {$this say \"carries on\"})");
    const hearth::Tally s = soak(kit(data, steady), 1, 2);
    CHECK(s.interrupts == 0);
    CHECK(s.oscillating == 0);
}

TEST_CASE("hearth: why reports the options, the pick, and the margin") {
    const hearth::Kit k = kit();
    baker::Context ctx;
    baker::Scheduler clock(ctx);
    std::vector<std::string> errors;
    hearth::prepare(ctx, clock, k, &errors);
    CHECK(errors.empty());
    hearth::Household h(k, 0, ctx, clock, 7, "h.");
    for (int i = 0; i < 10; ++i) h.step(30);
    hearth::Villager* ada = h.villager("ada");
    REQUIRE(ada);
    const std::string why = h.why(*ada);
    CHECK(why.find("ada, day 1") == 0);
    CHECK(why.find(" > ") != std::string::npos);
    CHECK(why.find("margin") != std::string::npos);
    // The villagers are objects scripts can reach by name.
    auto hunger = ctx.eval(*baker::read_string("{h.ada need hunger}").value()->nodes().begin());
    REQUIRE(hunger);
    CHECK(hunger->as_float().value_or(-1) > 0);
}
