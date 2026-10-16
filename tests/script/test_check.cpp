// baker::check and the generated reference (spec §5, M4).
#include <doctest.h>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/script.hpp>
#include <baker/std_script.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "ok.hpp"
#include "temp_dir.hpp"

using namespace baker;
using baker::test::ok;

namespace {

Ref<Array> parse(std::string_view text, std::string_view name) {
    auto r = read_string(text, name);
    REQUIRE(r);
    return *r;
}

}  // namespace

TEST_CASE("check catches a wrong-arity call at load, without evaluating anything") {
    Context ctx;
    int runs = 0;
    REQUIRE(ctx.func("spawn", [&](Symbol, int64_t) { return ++runs; }));
    const auto tree = parse(
        "(level\n"
        "  (on_enter {spawn grunt 3})\n"
        "  (on_exit {spawn grunt})\n"
        "  (on_win {spawn grunt three}))\n",
        "level.dta");
    const CheckReport report = check(*tree, ctx);
    CHECK(runs == 0);
    REQUIRE(report.errors.size() == 2);
    CHECK(to_string(report.errors[0]) ==
          "baker: 'spawn' takes 2 arguments, given 1  (level.dta:3)");
    CHECK(to_string(report.errors[1]) ==
          "baker: 'spawn' is given (symbol symbol); it takes (symbol int)  (level.dta:4)");
    CHECK_FALSE(report.ok());
}

TEST_CASE("check_validator checks script at every merge, beside other validators") {
    Context ctx;
    REQUIRE(ctx.func("spawn", [](Symbol, int64_t n) { return n; }));
    Config cfg({.tweaks = nullptr});
    std::vector<std::string> seen;
    cfg.set_warning_handler([&](const Error& e) { seen.emplace_back(to_string(e)); });
    // Validators run in the order they were added (D102).
    const auto first =
        cfg.add_validator([&](const ValidatorStack&, const Callback<void(const Error&)>&) {
            seen.emplace_back("first");
        });
    cfg.add_validator(check_validator(ctx));
    REQUIRE(cfg.push_layer("base", parse("(level (on_enter {spawn grunt 3}))", "base.dta")));
    CHECK(seen == std::vector<std::string>{"first"});

    // An overlay with a wrong call: reported at the push, located, and nothing runs.
    seen.clear();
    REQUIRE(cfg.push_layer("event", parse("(level\n  (on_exit {spawn grunt}))", "event.dta")));
    CHECK(seen == std::vector<std::string>{
                      "first", "baker: 'spawn' takes 2 arguments, given 1  (event.dta:2)"});

    // Removing a validator stops it; the check runs at every merge.
    cfg.remove_validator(first);
    seen.clear();
    REQUIRE(cfg.push_layer("fix", parse("(level (on_exit {spawn grunt 1}))", "fix.dta")));
    CHECK(seen.empty());
    if constexpr (!kShipping) {
        REQUIRE(cfg.set_layer_enabled("fix", false));
        CHECK(seen ==
              std::vector<std::string>{"baker: 'spawn' takes 2 arguments, given 1  (event.dta:2)"});
    }
}

TEST_CASE("check skips what it cannot know before running") {
    Context ctx;
    REQUIRE(ctx.func("spawn", [](Symbol, int64_t n) { return n; }));
    REQUIRE(ctx.func("log", [](Args a) { return int64_t(a.size()); }));
    const auto tree = parse(
        "{spawn grunt {+ 1 2}}\n"  // a command argument: type unknown until run
        "{spawn grunt $count}\n"   // a variable
        "{log 1 two \"three\"}\n"  // a rest parameter
        "{if 1 anything}\n"        // an unevaluated parameter
        "{+ 1 2.5}\n"              // Int promotes to Float
        "{set $x 1}\n",
        "ok.dta");
    const CheckReport report = check(*tree, ctx);
    CHECK(report.errors.empty());
    CHECK(report.demand.empty());
}

TEST_CASE("check: literal types against overloads, and {set} needs a variable") {
    Context ctx;
    const auto tree = parse("{+ 1 two}\n{set x 1}\n{not}\n", "bad.dta");
    const CheckReport report = check(*tree, ctx);
    REQUIRE(report.errors.size() == 3);
    CHECK(report.errors[0].message() ==
          "'+' is given (int symbol); it takes (int int) (float float)");
    CHECK(report.errors[1].message() == "'set' is given (symbol int); it takes ($variable value)");
    CHECK(report.errors[2].message() == "'not' takes 1 argument, given 0");
    CHECK(report.errors[2].line() == 3);
}

TEST_CASE("the demand report lists unresolved heads and stubs with every call site") {
    Context ctx;
    const auto a = parse(
        "{func boss.taunt ($who) (status stub) 0}\n"
        "{func helper ($x) $x}\n"
        "(phase1 {boss.find_cover 1} {boss.taunt player})\n"
        "(phase2\n  {boss.find_cover 2}\n  {helper 1}\n  {helper 1 2})\n",
        "boss.dta");
    const auto b = parse("{boss.find_cover 3}\n", "arena.dta");
    CheckReport report;
    check(*a, ctx, report);
    check(*b, ctx, report);
    REQUIRE(report.errors.size() == 1);
    CHECK(to_string(report.errors[0]) == "baker: 'helper' takes 1 argument, given 2  (boss.dta:7)");
    REQUIRE(report.demand.size() == 2);
    CHECK(report.demand[0].name == Symbol("boss.find_cover"));
    CHECK_FALSE(report.demand[0].stub);
    REQUIRE(report.demand[0].sites.size() == 3);
    CHECK(report.demand[0].sites[0].line == 3);
    CHECK(report.demand[0].sites[1].line == 5);
    CHECK(report.demand[0].sites[2].file == Symbol("arena.dta"));
    CHECK(report.demand[1].name == Symbol("boss.taunt"));
    CHECK(report.demand[1].stub);
    CHECK(
        to_string(report) ==
        "baker: 'helper' takes 1 argument, given 2  (boss.dta:7)\n"
        "demand: 'boss.find_cover' is not defined, called at boss.dta:3, boss.dta:5, arena.dta:1\n"
        "demand: 'boss.taunt' is a stub, called at boss.dta:3\n");
}

TEST_CASE("check over the corpus's scripts, with their functions registered by arity") {
    Context ctx;
    auto tree = read_file(BAKER_CORPUS_DIR "/scripts.dta");
    REQUIRE(tree);
    CheckReport report = check(**tree, ctx);
    // Everything the fixture calls is reported as demand; nothing is an error yet.
    CHECK(report.errors.empty());
    // Stub each demanded head with a variadic native, as a harness growing the vocabulary
    // would; then the report is clean.
    for (const auto& d : report.demand)
        REQUIRE(ctx.func(d.name, [](Args a) { return int64_t(a.size()); }));
    report = check(**tree, ctx);
    CHECK(report.errors.empty());
    CHECK(report.demand.empty());
    MESSAGE(to_string(report));
}

TEST_CASE("docs/script-reference.md matches the code") {
    Context ctx;
    const std::string generated = reference(ctx);
    const std::string path = BAKER_SOURCE_DIR "/docs/script-reference.md";
    if (std::getenv("BAKER_UPDATE_REFERENCE")) REQUIRE(write_reference(ctx, path));
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    std::string on_disk = s.str();
    on_disk.erase(std::remove(on_disk.begin(), on_disk.end(), '\r'), on_disk.end());
    CHECK_MESSAGE(on_disk == generated,
                  "docs/script-reference.md is out of date: run the script tests with "
                  "BAKER_UPDATE_REFERENCE=1 to regenerate it");
}

TEST_CASE("check applies the one-way rule to calls across packs (D80)") {
    // An in-memory file system: three packs and a loose file.
    std::map<std::string, std::string> files{
        {"task.dta",
         "(pack boss_task (level task))\n{func boss.restart_phase () 1}\n"
         "{func boss.use_engine () {engine.shake}}"},
        {"engine.dta", "(pack engine_core (level engine))\n{func engine.shake () 0}"},
        {"game.dta",
         "(pack game_rules (level game))\n#require engine.dta\n#require task.dta\n"
         "{func game.tick () {boss.restart_phase}}\n{boss.restart_phase}"},
        {"loose.dta", "#require task.dta\n{boss.restart_phase}"},
    };
    LoadSession session;
    session.set_reader([&](std::string_view path, TextSink out) {
        auto it = files.find(std::string(path));
        if (it == files.end()) return false;
        out(it->second);
        return true;
    });
    session.set_pack_levels({"engine", "game", "task"});
    Context ctx;
    auto game = test::ok(read_file("game.dta", session));
    const CheckReport report = check(*game, ctx, {.packs = &session});
    // game_rules calls boss.restart_phase twice (lines 4 and 5): warned once, at the first.
    // boss_task calling engine.shake is softer calling harder: fine.
    REQUIRE(report.warnings.size() == 1);
    CHECK(report.warnings[0].file() == Symbol("game.dta"));
    CHECK(report.warnings[0].line() == 4);
    CHECK(to_string(report.warnings[0])
              .find("pack 'game_rules' (level game) calls 'boss.restart_phase' from the softer "
                    "pack 'boss_task' (level task)") != std::string::npos);
    CHECK(report.errors.empty());
    CHECK(to_string(report).find("warning: baker: pack 'game_rules'") != std::string::npos);

    // Without the session, or from a file that declares no pack, nothing is warned.
    CHECK(check(*game, ctx).warnings.empty());
    auto loose = test::ok(read_file("loose.dta", session));
    CHECK(check(*loose, ctx, {.packs = &session}).warnings.empty());

    // A script function the Context has registered is attributed by its file too.
    auto task = test::ok(read_file("task.dta", session));
    for (const Node& n : *task) (void)test::ok(ctx.eval(n));
    files["call.dta"] = "(pack more_rules (level game))\n{boss.restart_phase}";
    auto call = test::ok(read_file("call.dta", session));
    const CheckReport registered = check(*call, ctx, {.packs = &session});
    REQUIRE(registered.warnings.size() == 1);
    CHECK(registered.warnings[0].file() == Symbol("call.dta"));
}

TEST_CASE("check reports a pack with one owner, and repetition across its trees (D94)") {
    Context ctx;
    LoadSession session;
    session.set_pack_levels({"game", "task"});
    const auto solo =
        ok(read_string("(pack boss (level task) (owners \"td-a\"))\n(x 1)", "boss.dta", session));
    const auto pair = ok(
        read_string("(pack econ (level game) (owners \"a\" \"b\"))\n(y 1)", "econ.dta", session));
    const auto none = ok(read_string("(pack ui (level game))\n(z 1)", "ui.dta", session));
    CheckReport report;
    check(*solo, ctx, report, {.packs = &session});
    REQUIRE(report.findings.size() == 1);
    CHECK(to_string(report.findings[0]) ==
          "baker: pack 'boss' has one owner, td-a: no one else keeps it  (boss.dta)");
    CHECK(report.ok());                              // a finding is never an error
    check(*pair, ctx, report, {.packs = &session});  // accumulates, without repeating it
    CHECK(report.findings.size() == 1);
    CHECK(check(*none, ctx).findings.empty());  // without the session, no findings

    // Repetition across the trees of one call, and in the text form.
    const auto a = ok(read_string("(e (hp 30) (speed 2) (loot coin))", "a.dta"));
    const auto b = ok(read_string("(e (hp 30) (speed 2) (loot coin))", "b.dta"));
    const auto c = ok(read_string("(e (hp 30) (speed 2) (loot coin))", "c.dta"));
    const Array* trees[] = {a.get(), b.get(), c.get()};
    CheckReport r;
    check(trees, ctx, r);
    REQUIRE(r.repeats.size() == 1);
    CHECK(to_string(r) ==
          "repeat: 3 times, 10 nodes: (e (hp 30) (speed 2) (loot coin)) at a.dta:1, b.dta:1, "
          "c.dta:1\n");
    CheckReport off;
    check(trees, ctx, off, {.repeats = {.min_repeats = 0}});
    CHECK(off.repeats.empty());
}

TEST_CASE("check's JSON is versioned, sorted, and the same bytes on every run (D95)") {
    // Every section has something: an error, a one-way warning, demand from two files, a
    // repeat whose form needs escaping, a one-owner finding, and packs with metadata.
    std::map<std::string, std::string> files{
        {"task.dta",
         "(pack boss_task (level task) (owners \"td-encounter\") (for designers) (stage custom))\n"
         "{func boss.restart () 1}\n"
         "(phase (say \"Get \\qready\\q\") (hp 30) (wait 2))\n"},
        {"game.dta",
         "(pack game_rules (level game) (requires boss_task) (owners \"a\" \"b\"))\n"
         "#require task.dta\n"
         "{boss.restart}\n{spawn grunt}\n{+ 1}\n"
         "(phase (say \"Get \\qready\\q\") (hp 30) (wait 2))\n"},
        {"level.dta", "{spawn grunt}\n(phase (say \"Get \\qready\\q\") (hp 30) (wait 2))\n"},
    };
    auto run = [&] {
        LoadSession session;
        session.set_reader([&](std::string_view path, TextSink out) {
            auto it = files.find(std::string(path));
            if (it == files.end()) return false;
            out(it->second);
            return true;
        });
        session.set_pack_levels({"game", "task"});
        Context ctx;
        auto game = test::ok(read_file("game.dta", session));
        auto level = test::ok(read_file("level.dta", session));
        const Array* trees[] = {game.get(), level.get()};
        CheckReport report;
        check(trees, ctx, report, {.packs = &session, .repeats = {.min_nodes = 6}});
        return to_json(report, &session);
    };
    const std::string first = run();
    CHECK(run() == first);

    const std::string path = BAKER_SOURCE_DIR "/tests/script/check_expected.json";
    if (std::getenv("BAKER_UPDATE_REFERENCE")) {
        std::ofstream(path, std::ios::binary) << first;
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    std::string expected = s.str();
    expected.erase(std::remove(expected.begin(), expected.end(), '\r'), expected.end());
    CHECK_MESSAGE(expected == first,
                  "tests/script/check_expected.json differs: run the script tests with "
                  "BAKER_UPDATE_REFERENCE=1 to regenerate it, and review the diff");

    // Without the session, the packs list is empty; an empty report is still a whole one.
    CHECK(to_json(CheckReport{}) ==
          "{\n  \"schema\": \"baker.check/1.2\",\n  \"passed\": true,\n  \"errors\": [],\n"
          "  \"warnings\": [],\n  \"demand\": [],\n  \"repeats\": [],\n  \"findings\": [],\n"
          "  \"load\": [],\n  \"shape_errors\": [],\n  \"shape_warnings\": [],\n"
          "  \"packs\": []\n}\n");
}

TEST_CASE("the reference lists the session's packs and origins: the harness register (D95)") {
    LoadSession session;
    session.set_pack_levels({"game"});
    (void)test::ok(
        read_string("(pack econ (level game) (owners \"td-econ\") (for designers) (stage product))",
                    "econ.dta", session));
    (void)test::ok(read_string("(origin \"economy.xlsx\" (tool sheets-export))\n(price 5)",
                               "prices.dta", session));
    Context ctx;
    const std::string with = reference(ctx, &session);
    CHECK(with.find("## Packs\n\n| Pack | Level | Requires | Owners | For | Stage | File |\n"
                    "|---|---|---|---|---|---|---|\n"
                    "| `econ` | game | - | td-econ | designers | product | econ.dta |\n") !=
          std::string::npos);
    CHECK(with.find("| sheets-export | economy.xlsx | prices.dta |") != std::string::npos);
    CHECK(reference(ctx).find("## Packs") == std::string::npos);  // without the session
}

TEST_CASE("a Config's read_session has every layer's packs and origins (D110)") {
    std::map<std::string, std::string> files{
        {"default.dta",
         "(pack game_rules (level game) (owners \"a\"))\n#require task.dta\n"
         "{func game.tick () {boss.restart}}\n(x 1)"},
        {"task.dta",
         "(pack boss_task (level task) (owners \"a\" \"b\"))\n{func boss.restart () 1}"},
        {"agent.dta", "(origin \"sheet.xlsx\" (tool export))\n(x 2)"},
    };
    Config cfg({.tweaks = nullptr});
    cfg.session().set_reader([&](std::string_view path, TextSink out) {
        auto it = files.find(std::string(path));
        if (it == files.end()) return false;
        out(it->second);
        return true;
    });
    cfg.session().set_pack_levels({"engine", "game", "task"});
    Context ctx;
    std::vector<std::string> warned;
    cfg.set_warning_handler([&](const Error& e) { warned.emplace_back(to_string(e)); });
    cfg.add_validator(check_validator(ctx, {.packs = &cfg.read_session()}));
    REQUIRE(cfg.push_layer("default", "default.dta"));
    REQUIRE(cfg.push_layer("agent", "agent.dta"));

    // The base session has none of it: each layer read a copy.
    CHECK(cfg.session().packs().empty());
    const LoadSession& read = cfg.read_session();
    REQUIRE(read.packs().size() == 2);
    REQUIRE(read.origin_of(Symbol("agent.dta")));
    CHECK(read.origin_of(Symbol("agent.dta"))->source == Symbol("sheet.xlsx"));

    // The validator, given it, applies the one-way rule through the Config.
    CHECK(std::any_of(warned.begin(), warned.end(), [](const std::string& w) {
        return w.find("calls 'boss.restart' from the softer pack 'boss_task'") != std::string::npos;
    }));
    // And check finds the single-owner pack from it.
    const CheckReport report = check(*cfg.root(), ctx, {.packs = &read});
    REQUIRE(report.findings.size() == 1);
    CHECK(to_string(report.findings[0]).find("'game_rules' has one owner") != std::string::npos);

    // Removing the layer that declared the origin drops it at the next merge.
    REQUIRE(cfg.remove_layer("agent"));
    CHECK_FALSE(cfg.read_session().origin_of(Symbol("agent.dta")));
    CHECK(cfg.read_session().packs().size() == 2);
}

// --- The gate (dev/check-gate-plan.md §1): a Config's #autorun script, host-filled problems,
// and a rule for unresolved calls. ---

TEST_CASE("checking a Config sees the script its #autorun commands define") {
    baker::test::TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer(
        "default",
        dir.write("default.dta",
                  "#autorun {func helper () {nosuch 1}}\n(level (on_enter {helper}))\n")));
    Context ctx;
    REQUIRE(ctx.run_autorun(cfg.autorun().items()).empty());  // defines helper, as a host does
    // The merged tree alone does not hold the #autorun commands.
    CHECK(check(*cfg.root(), ctx).demand.empty());
    CheckReport report;
    check(cfg, ctx, report);
    REQUIRE(report.demand.size() == 1);
    CHECK(report.demand[0].name == Symbol("nosuch"));
    REQUIRE(report.demand[0].sites.size() == 1);
    CHECK(report.demand[0].sites[0].line == 1);
    CHECK(report.ok());  // unresolved calls fail only when asked

    CheckReport strict;
    check(cfg, ctx, strict, {.unresolved_fails = true});
    CHECK_FALSE(strict.ok());
    CHECK(to_json(strict).find("\"passed\": false") != std::string::npos);
}

TEST_CASE("problems the host found fail the check and are in its JSON") {
    CheckReport report;
    CHECK(report.ok());
    const Error shape(ErrorCode::Range, "speed 12 is out of range 0..10", Symbol("d.dta"), 3);
    report.shape_warnings = List<Error>(std::span<const Error>(&shape, 1));
    CHECK(report.ok());  // a warning never fails
    report.shape_errors = List<Error>(std::span<const Error>(&shape, 1));
    CHECK_FALSE(report.ok());
    const std::string json = to_json(report);
    CHECK(json.find("\"passed\": false") != std::string::npos);
    CHECK(json.find(
              "\"shape_errors\": [\n    {\"code\": \"range\", \"file\": \"d.dta\", "
              "\"line\": 3, \"column\": 0, \"path\": \"\", \"message\": \"speed 12 is out of range 0..10\"}") !=
          std::string::npos);

    CheckReport loaded;
    const Error failed(ErrorCode::NotFound, "'spawnn' is not a function", Symbol("d.dta"), 1);
    loaded.load = List<Error>(std::span<const Error>(&failed, 1));
    CHECK_FALSE(loaded.ok());
    CHECK(to_string(loaded).rfind("load: ", 0) == 0);
}
