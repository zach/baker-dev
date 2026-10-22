// Tweaks a tool makes from the reference's descriptions, held to as the game's tweaks are
// (D210, D212, D215).
#include <doctest.h>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/tools/described.hpp>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "ok.hpp"

using namespace baker;
using namespace baker::tools;
using baker::test::ok;

namespace {
std::vector<std::string> messages(const List<Error>& list) {
    std::vector<std::string> out;
    for (const Error& e : list) out.emplace_back(e.message());
    return out;
}
}  // namespace

namespace {
struct Vec2 {
    float x = 0, y = 0;
};
}  // namespace

template <>
struct baker::Convert<Vec2> {
    static constexpr std::string_view name = "vec2";
    static constexpr std::string_view values[] = {"float32", "float32"};
    static bool from(const Values& v, Vec2& out) {
        return v.size() >= 2 && v.read(0, out.x) && v.read(1, out.y);
    }
    static void to(const Vec2& v, ValueList& out) {
        out.add(v.x);
        out.add(v.y);
    }
};

TEST_CASE("a tool registers tweaks from their descriptions, and check holds data to them") {
    // The game's own tweaks, and a tool's copy of them from the reference's descriptions.
    TweakRegistry game;
    Tweak<float> height{"jump.height", 120.0f, {.range = {16, 320}}, game};
    Tweak<Vec2> aim{"jump.aim", Vec2{0, 1}, {}, game};
    static constexpr std::string_view kSurfaces[] = {"stone", "ice"};
    Tweak<Symbol> floor{"world.floor", Symbol("stone"), {.one_of = kSurfaces}, game};

    TweakRegistry tool;
    DescribedTweaks described(tool);
    static constexpr std::string_view kVec2[] = {"float32", "float32"};
    REQUIRE(described.add(
        {.path = "jump.height", .type = "float32", .default_text = "120.0", .range = {16, 320}}));
    REQUIRE(described.add(
        {.path = "jump.aim", .type = "vec2", .values = kVec2, .default_text = "(0.0 1.0)"}));
    REQUIRE(described.add(
        {.path = "world.floor", .type = "symbol", .default_text = "stone", .one_of = kSurfaces}));
    CHECK(described.size() == 3);
    CHECK(tool.size() == 3);

    // Each finding is the game's own, for the same data.
    for (const char* text : {"(jump (height 120) (aim 0 1))\n(world (floor ice))\n",
                             "(jump (height \"tall\") (aim 0 1))\n(world (floor ice))\n",
                             "(jump (height 1000) (aim 1))\n(world (floor lava))\n",
                             "(jump (hieght 120) (aim 0.5 x))\n"}) {
        CAPTURE(text);
        const Ref<Array> root = ok(read_string(text, "d.dta"));
        const TweakFindings mine = check_tweaks(*root, tool);
        const TweakFindings theirs = check_tweaks(*root, game);
        CHECK(messages(mine.errors) == messages(theirs.errors));
        CHECK(messages(mine.warnings) == messages(theirs.warnings));
    }
    const TweakFindings wrong =
        check_tweaks(*ok(read_string("(jump (height 120) (aim 1))\n", "d.dta")), tool);
    REQUIRE(wrong.errors.size() == 1);  // a vec2 wanted
    CHECK(std::string(wrong.errors[0].message()).find("vec2") != std::string::npos);

    // The JSON of the described tweaks names their types as the game's does.
    std::string json;
    write_json(tool, string_out(json));
    CHECK(json.find(
              R"j("type": "vec2", "values": ["float32", "float32"], "default": "(0.0 1.0)")j") !=
          std::string::npos);
}

TEST_CASE("a description the registry cannot hold is an error naming the tweak") {
    TweakRegistry reg;
    DescribedTweaks described(reg);
    const auto no_values = described.add({.path = "jump.aim", .type = "vec2"});
    REQUIRE_FALSE(no_values);
    CHECK(std::string(no_values.error().message()).find("'jump.aim'") != std::string::npos);
    static constexpr std::string_view kUnknown[] = {"float64"};
    const auto unknown = described.add({.path = "a", .type = "big", .values = kUnknown});
    REQUIRE_FALSE(unknown);
    CHECK(std::string(unknown.error().message()).find("float64") != std::string::npos);
    const auto bad_default =
        described.add({.path = "b", .type = "float32", .default_text = "tall"});
    REQUIRE_FALSE(bad_default);
    CHECK(std::string(bad_default.error().message()).find("default") != std::string::npos);
    CHECK(described.size() == 0);
    CHECK(reg.size() == 0);
}

TEST_CASE("a fixed-size list declares its value types, and a tool checks it as the game (D212)") {
    TweakRegistry game;
    Tweak<std::array<float, 3>> color{"cube.color", {0.9f, 0.5f, 0.2f}, {}, game};
    TweakRegistry nested;
    Tweak<std::array<Vec2, 2>> path{"cube.path", {}, {}, nested};
    REQUIRE(color.value_types().size() == 3);
    CHECK(path.value_types().empty());  // values nested in each element: not listed flat
    std::string json;
    write_json(game, string_out(json));
    CHECK(json.find(
              R"j("type": "fixed-size list", "values": ["float32", "float32", "float32"], )j") !=
          std::string::npos);

    TweakRegistry tool;
    DescribedTweaks described(tool);
    REQUIRE(described.add({.path = "cube.color",
                           .type = "fixed-size list",
                           .values = color.value_types(),
                           .default_text = "(0.9 0.5 0.2)"}));
    for (const char* text : {"(cube (color 0.9 0.5 0.2))\n", "(cube (color 0.9 0.5))\n",
                             "(cube (color red 0.5 0.2))\n"}) {
        CAPTURE(text);
        const Ref<Array> root = ok(read_string(text, "d.dta"));
        CHECK(messages(check_tweaks(*root, tool).errors) ==
              messages(check_tweaks(*root, game).errors));
        CHECK(messages(check_tweaks(*root, tool).warnings) ==
              messages(check_tweaks(*root, game).warnings));
    }
}

TEST_CASE("string and integer tweaks take allowed values, checked and in the JSON (D215)") {
    TweakRegistry reg;
    static constexpr std::string_view kAttacks[] = {"light", "heavy", "kick"};
    Tweak<std::string_view> attack{"fighter.attack", "light", {.one_of = kAttacks}, reg};
    static constexpr TweakChoice kLevels[] = {{0, "easy"}, {1, "normal"}, {2, "hard"}};
    Tweak<int> level{"fighter.level", 1, {.choices = kLevels}, reg};
    Tweak<int> team{"fighter.team", 0, {}, reg};
    const auto run = [&](const char* text) {
        return messages(check_tweaks(*ok(read_string(text, "d.dta")), reg).errors);
    };

    CHECK(
        run("(fighter (attack \"heavy\") (level 2) (team 0 (one_of (0 red) (1 blue))))\n").empty());
    auto bad = run("(fighter (attack \"hevy\") (level 5) (team 3 (one_of (0 red) 1)))\n");
    REQUIRE(bad.size() == 3);
    CHECK(bad[0] == "'fighter.attack' is 'hevy', not one of its tweak's light, heavy, kick");
    CHECK(bad[1] == "'fighter.level' is 5, not one of its tweak's 0 (easy), 1 (normal), 2 (hard)");
    CHECK(bad[2] == "'fighter.team' is 3, not one of its tweak's 0 (red), 1");  // the data's
    // The data's own list wins over the code's.
    CHECK(run("(fighter (attack \"punch\" (one_of \"punch\" \"kick\")) (level 7 (one_of 7)))\n")
              .empty());
    CHECK(attack.one_of().size() == 3);
    REQUIRE(level.choices().size() == 3);
    CHECK(level.choices()[2].name == "hard");

    std::string json;
    write_json(reg, string_out(json));
    CHECK(json.find(R"j("one_of": ["light", "heavy", "kick"], "choices": [])j") !=
          std::string::npos);
    CHECK(
        json.find(R"j("choices": [{"value": 0, "name": "easy"}, {"value": 1, "name": "normal"}, )j"
                  R"j({"value": 2, "name": "hard"}])j") != std::string::npos);

    // A tool's described copy finds the same.
    TweakRegistry tool;
    DescribedTweaks described(tool);
    REQUIRE(described.add({.path = "fighter.attack",
                           .type = "string",
                           .default_text = "\"light\"",
                           .one_of = kAttacks}));
    REQUIRE(described.add(
        {.path = "fighter.level", .type = "int32", .default_text = "1", .choices = kLevels}));
    const Ref<Array> root = ok(read_string("(fighter (attack \"hevy\") (level 5))\n", "d.dta"));
    TweakRegistry only;  // the game's two, without team
    Tweak<std::string_view> attack2{"fighter.attack", "light", {.one_of = kAttacks}, only};
    Tweak<int> level2{"fighter.level", 1, {.choices = kLevels}, only};
    CHECK(messages(check_tweaks(*root, tool).errors) == messages(check_tweaks(*root, only).errors));
}
