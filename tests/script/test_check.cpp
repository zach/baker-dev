// moira::check and the generated reference (spec §5, M4).
#include <doctest.h>
#include <moira/parse.hpp>
#include <moira/script.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "ok.hpp"

using namespace moira;
using moira::test::ok;

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
          "moira: 'spawn' takes 2 arguments, given 1  (level.dta:3)");
    CHECK(to_string(report.errors[1]) ==
          "moira: 'spawn' is given (symbol symbol); it takes (symbol int)  (level.dta:4)");
    CHECK_FALSE(report.ok());
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
    CHECK(report.errors[0].message ==
          "'+' is given (int symbol); it takes (int int) (float float)");
    CHECK(report.errors[1].message == "'set' is given (symbol int); it takes ($variable value)");
    CHECK(report.errors[2].message == "'not' takes 1 argument, given 0");
    CHECK(report.errors[2].line == 3);
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
    CHECK(to_string(report.errors[0]) == "moira: 'helper' takes 1 argument, given 2  (boss.dta:7)");
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
        report.to_string() ==
        "moira: 'helper' takes 1 argument, given 2  (boss.dta:7)\n"
        "demand: 'boss.find_cover' is not defined, called at boss.dta:3, boss.dta:5, arena.dta:1\n"
        "demand: 'boss.taunt' is a stub, called at boss.dta:3\n");
}

TEST_CASE("check over the corpus's scripts, with their functions registered by arity") {
    Context ctx;
    auto tree = read_file(MOIRA_CORPUS_DIR "/scripts.dta");
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
    MESSAGE(report.to_string());
}

TEST_CASE("docs/script-reference.md matches the code") {
    Context ctx;
    const std::string generated = reference(ctx);
    const std::string path = MOIRA_SOURCE_DIR "/docs/script-reference.md";
    if (std::getenv("MOIRA_UPDATE_REFERENCE")) REQUIRE(write_reference(ctx, path));
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    std::string on_disk = s.str();
    on_disk.erase(std::remove(on_disk.begin(), on_disk.end(), '\r'), on_disk.end());
    CHECK_MESSAGE(on_disk == generated,
                  "docs/script-reference.md is out of date: run the script tests with "
                  "MOIRA_UPDATE_REFERENCE=1 to regenerate it");
}
