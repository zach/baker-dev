// baker::shape (spec §6, D84): declarations, checking, enumeration, and the Config hook.
#include <doctest.h>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/shape.hpp>
#include <baker/std_interop.hpp>

#include "ok.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace baker;
using baker::test::ok;

namespace {

const std::string kDir = BAKER_SHAPE_DIR;

std::vector<std::string> texts(const List<Error>& list) {
    std::vector<std::string> out;
    for (const Error& e : list) out.emplace_back(to_string(e));
    return out;
}

shape::Report check_text(std::string_view text) {
    auto tree = ok(read_string(text, "t.dta"));
    shape::Set set;
    shape::Report report;
    set.add(*tree, report);
    shape::check(*tree, set, report);
    return report;
}

}  // namespace

TEST_CASE("a violation in corpus-style data is reported at load, located") {
    Config cfg({.tweaks = nullptr});
    std::vector<std::string> reported;
    cfg.set_warning_handler([&](const Error& e) { reported.emplace_back(to_string(e)); });
    cfg.add_validator(shape::validator());
    REQUIRE(cfg.push_layer("bosses", kDir + "/bosses.dta"));
    // Before the patch: the dragon's third phase is out of range, the golem's phase lacks
    // its cadence (errors), and the golem's taunts is not declared (a warning).
    const auto has = [&](const std::string& part) {
        return std::any_of(reported.begin(), reported.end(),
                           [&](const std::string& r) { return r.find(part) != std::string::npos; });
    };
    CHECK(reported.size() == 3);
    CHECK(has("'enter_at' is 1.5, outside 0.0 to 1.0  (" + kDir +
              "/bosses.dta:23, at "
              "bosses/boss/phases/boss_phase/enter_at)"));
    CHECK(has("'boss_phase' has no 'cadence'  (" + kDir + "/bosses.dta:30"));
    CHECK(has("'taunts' is not a field of shape 'boss'"));
    // The tutorial's boss_phase is outside (at bosses boss phases): not checked.
    CHECK_FALSE(has("tutorial"));

    // The patch layer retunes the range and declares taunts: one error remains.
    reported.clear();
    REQUIRE(cfg.push_layer("patch", kDir + "/bosses_patch.dta"));
    CHECK(reported.size() == 1);
    CHECK(has("has no 'cadence'"));
}

TEST_CASE("types, lists, nested shapes, and what is not checked") {
    const auto r = check_text(R"(
        (shapes
          (vec (x float) (y float))
          (unit (name string) (alive bool) (speed float (range 0 10)) (tags (list symbol))
                (pos vec) (path (list vec)) (data any) (count int)))
        (unit (name "bob") (alive 1) (speed {compute}) (tags a b 3) (pos (x 1) (y "no"))
              (path ((x 1) (y 2)) (p2 (x 1))) (data whatever 1 2) (count 2.5) (extra 1) 7)
    )");
    const auto e = texts(r.errors);
    const auto w = texts(r.warnings);
    auto has = [](const std::vector<std::string>& list, const std::string& part) {
        return std::any_of(list.begin(), list.end(),
                           [&](const std::string& s) { return s.find(part) != std::string::npos; });
    };
    CHECK(has(e, "'tags' is int, not symbol"));  // a list's element
    CHECK(has(e, "'y' is string, not float"));   // inside a nested shape
    CHECK(has(e, "'vec' has no 'y'"));           // in a list of shapes, the second
    CHECK(has(e, "'count' is float, not int"));
    CHECK(e.size() == 4);  // {compute} and `any` pass
    CHECK(has(w, "'extra' is not a field of shape 'unit'"));
    CHECK(has(w, "a value that is not a field of shape 'unit'"));
    CHECK(w.size() == 2);
}

TEST_CASE("malformed declarations are located errors; unknown shapes are reported") {
    const auto r = check_text(R"((shapes (s (a) (b float (range 2 1)) (c (list)) 5) 7)
                                  (s (c 1) (d 2)) (shapes (t (u nowhere))) (t (u 1)))");
    const auto e = texts(r.errors);
    CHECK(e.size() == 6);
    CHECK(std::count_if(e.begin(), e.end(), [](const std::string& s) {
              return s.find("t.dta:1)") != std::string::npos;
          }) == 5);
    CHECK(std::any_of(e.begin(), e.end(), [](const std::string& s) {
        return s.find("'u' is of the unknown shape 'nowhere'") != std::string::npos;
    }));
}

TEST_CASE("shapes are enumerable, with their metadata, for tuning panels") {
    auto tree = ok(read_file(kDir + "/bosses.dta"));
    auto patch = ok(read_file(kDir + "/bosses_patch.dta"));
    shape::Set set;
    shape::Report report;
    set.add(*tree, report);
    set.add(*patch, report);
    CHECK(report.ok());
    REQUIRE(set.shapes().size() == 2);
    const shape::Shape* phase = set.find("boss_phase");
    REQUIRE(phase);
    REQUIRE(phase->at.size() == 3);
    CHECK(phase->at[1] == Symbol("boss"));
    REQUIRE(phase->fields.size() == 4);
    const shape::Field* enter = phase->field("enter_at");
    REQUIRE(enter);
    CHECK(enter->type == shape::Type::Float);
    CHECK(enter->max == 2.0);  // the patch's range
    CHECK(enter->kind == Symbol("percent"));
    CHECK(enter->desc == "HP fraction that starts this phase");
    CHECK(phase->field("attacks")->type == shape::Type::List);
    CHECK(phase->field("attacks")->element == shape::Type::Symbol);
    CHECK(phase->field("music")->optional);
    CHECK(set.find("boss")->field("taunts")->optional);
    // A copy stands alone.
    shape::Set copy = set;
    set = shape::Set();
    CHECK(copy.find("boss_phase")->field("enter_at")->desc == "HP fraction that starts this phase");
}

TEST_CASE("a (keyed) shape governs every child at its path, whatever its tag (D108)") {
    const auto r = check_text(R"(
        (shapes
          (move (at fighters * moves) (keyed)
             (startup int (range 1 60))
             (cancels (list symbol) (optional))))
        (fighters
          (kestrel (moves
             (jab (startup 4) (cancels jab))
             (heavy (startup 900) (armor 1))
             (palm (cancels jab))))
          (wren (moves (poke (startup 3)))))
        (moves (stray (startup 900)))   ; not under fighters/*/moves
    )");
    const auto e = texts(r.errors);
    const auto w = texts(r.warnings);
    REQUIRE(e.size() == 2);
    CHECK(e[0].find("'startup' is 900, outside 1.0 to 60.0") != std::string::npos);
    CHECK(e[0].find("fighters/kestrel/moves/heavy/startup") != std::string::npos);
    CHECK(e[1].find("'move' has no 'startup'") != std::string::npos);  // palm
    REQUIRE(w.size() == 1);
    CHECK(w[0].find("'armor' is not a field of shape 'move'") != std::string::npos);

    const auto bad = check_text("(shapes (move (keyed) (startup int)))");
    REQUIRE(bad.errors.size() == 1);
    CHECK(texts(bad.errors)[0].find("no (at ...)") != std::string::npos);
}
