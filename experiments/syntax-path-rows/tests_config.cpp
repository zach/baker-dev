// Rows in a Config: the layer tools, provenance, write-back, promote and a layer policy. The
// first cases run in both builds, on the tree the reader gives; the rest need the parser's
// hook (BAKER_PATH_ROWS), since a file layer is read by Baker's own parser.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <doctest.h>

#include <string>
#include <vector>

#ifdef BAKER_PATH_ROWS
#include <baker/write.hpp>

#include "parse_harness.hpp"
#include "temp_dir.hpp"
#endif

using namespace baker;
using namespace baker::experiments;

namespace {

Ref<Array> rows(std::string_view text, std::string_view name = "agent.dta") {
    auto r = syntax::read(text, name);
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : std::string(describe(r.error()))));
    return *r;
}

constexpr std::string_view kDefault =
    "(fighters\n"
    "   (kestrel (health 1000) (walk_back 1.5) (moves (jab (startup 6) (recovery 9))))\n"
    "   (heron (health 950)))\n"
    "(cpu (easy (reaction 30)) (hard (reaction 12)))\n";

constexpr std::string_view kAgentRows =
    "; the agent's tuning\n"
    "(/fighters.kestrel.health 1100)   ; the turtle wins too often\n"
    "(/fighters.kestrel.walk_back 1.7)\n"
    "(/cpu.easy.reaction 26)\n";

constexpr std::string_view kAgentNested =
    "; the agent's tuning\n"
    "(fighters (kestrel (health 1100) (walk_back 1.7)))\n"
    "(cpu (easy (reaction 26)))\n";

std::vector<std::string> paths(const List<LayerEffectEntry>& effect) {
    std::vector<std::string> out;
    for (const LayerEffectEntry& e : effect) out.emplace_back(e.path.str());
    return out;
}

}  // namespace

TEST_CASE("two rows under one prefix are one array: layer_effect and diff see every leaf") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", rows(kDefault, "default.dta")));
    REQUIRE(cfg.push_layer("agent", rows(kAgentRows)));
    auto effect = cfg.layer_effect("agent");
    REQUIRE(effect);
    CHECK(paths(*effect) == std::vector<std::string>{"fighters.kestrel.health",
                                                     "fighters.kestrel.walk_back",
                                                     "cpu.easy.reaction"});
    CHECK((*effect)[0].below->try_int() == 1000);
    CHECK((*effect)[1].below->try_float() == doctest::Approx(1.5));
    CHECK((*effect)[2].below->try_int() == 30);
    auto d = cfg.diff("default", "agent");
    REQUIRE(d);
    size_t both = 0;
    for (const DiffEntry& e : *d) both += e.a && e.b;
    CHECK(both == 3);  // health, walk_back and reaction, each set in both
    CHECK(cfg.get<int>("fighters.kestrel.health") == 1100);
    CHECK(cfg.get<int>("fighters.heron.health") == 950);  // the merge is untouched
    // Folded: one array a tag, so first-of-tag readers (D134) see every leaf.
    REQUIRE(cfg.layer_root("agent"));
    CHECK(cfg.layer_root("agent")->size() == 2);
}

TEST_CASE("a removal row removes the key below") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", rows(kDefault, "default.dta")));
    REQUIRE(cfg.push_layer("agent", rows("(/fighters.heron #remove)\n(/cpu.easy.reaction 26)\n")));
    CHECK(!cfg.get<int>("fighters.heron.health"));
    CHECK(cfg.get<int>("fighters.kestrel.health") == 1000);
    CHECK(cfg.get<int>("cpu.easy.reaction") == 26);
}

#ifdef BAKER_PATH_ROWS

using baker::test::TempDir;

TEST_CASE("a rows layer: source and explain name the row's line") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", std::string(kDefault))));
    REQUIRE(cfg.push_layer("agent", dir.write("agent.dta", std::string(kAgentRows))));
    auto s = cfg.source("fighters.kestrel.walk_back");
    REQUIRE(s);
    CHECK(s->layer == Symbol("agent"));
    CHECK(s->file.str().ends_with("agent.dta"));
    CHECK(s->line == 3);
    CHECK(cfg.source("cpu.easy.reaction")->line == 4);
    auto e = cfg.explain("fighters.kestrel.health");
    REQUIRE(e);
    REQUIRE(e->size() == 2);
    CHECK((*e)[1].layer == Symbol("agent"));
    CHECK((*e)[1].source.line == 2);
    CHECK((*e)[0].source.line == 2);  // default.dta's kestrel line
}

TEST_CASE("write_back into a row replaces one token and keeps the comment") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", std::string(kDefault))));
    REQUIRE(cfg.push_layer("agent", dir.write("agent.dta", std::string(kAgentRows))));
    REQUIRE(cfg.set("fighters.kestrel.health", Node(1150)));
    REQUIRE(cfg.write_back("fighters.kestrel.health"));
    std::string want(kAgentRows);
    want.replace(want.find("1100"), 4, "1150");
    CHECK(dir.read("agent.dta") == want);
    CHECK(cfg.unsaved().empty());
    // A float over an integer token stays an integer (D183), in a row as anywhere.
    REQUIRE(cfg.set("cpu.easy.reaction", Node(24.0)));
    REQUIRE(cfg.write_back("cpu.easy.reaction"));
    want.replace(want.find("26"), 2, "24");
    CHECK(dir.read("agent.dta") == want);
    // A row with no value gains one after its key (it supplied none, so write_back_to).
    dir.write("agent.dta", "(/fighters.kestrel.health)\n");
    REQUIRE(cfg.reload());
    REQUIRE(cfg.set("fighters.kestrel.health", Node(1200)));
    REQUIRE(cfg.write_back_to("fighters.kestrel.health", "agent"));
    CHECK(dir.read("agent.dta") == "(/fighters.kestrel.health 1200)\n");
}

TEST_CASE("write_back_to adds a row to a file of rows, and to a file it creates") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", std::string(kDefault))));
    REQUIRE(cfg.push_layer("agent", dir.write("agent.dta", std::string(kAgentRows))));
    REQUIRE(cfg.push_layer("local", dir.path("local/me.dta"), {.optional = true}));
    // Under a prefix the file has (cpu.easy), and under one it lacks (fighters.heron).
    REQUIRE(cfg.set("cpu.easy.patience", Node(3)));
    REQUIRE(cfg.write_back_to("cpu.easy.patience", "agent"));
    REQUIRE(cfg.set("fighters.heron.health", Node(990)));
    REQUIRE(cfg.write_back_to("fighters.heron.health", "agent"));
    const std::string written = dir.read("agent.dta");
    CHECK(written ==
          std::string(kAgentRows) + "(/cpu.easy.patience 3)\n(/fighters.heron.health 990)\n");
    CHECK(cfg.source("fighters.heron.health")->line == 6);
    CHECK(cfg.unsaved().empty());
    // The file reads back as its nested spelling.
    auto back = read_string(written, "agent.dta");
    auto nested = read_string(
        "(fighters (kestrel (health 1100) (walk_back 1.7)) (heron (health 990)))\n"
        "(cpu (easy (reaction 26) (patience 3)))\n",
        "agent.dta");
    REQUIRE(back);
    REQUIRE(nested);
    CHECK(first_difference(**back, **nested).empty());
    // A layer's absent file is created holding a row.
    REQUIRE(cfg.set("player.speed", Node(4.5)));
    REQUIRE(cfg.write_back_to("player.speed", "local"));
    CHECK(dir.read("local/me.dta") == "(/player.speed 4.5)\n");
    // A nested file stays nested.
    REQUIRE(cfg.write_back_to("cpu.easy.patience", "default"));
    CHECK(dir.read("default.dta").find("(easy (reaction 30) (patience 3))") != std::string::npos);
}

TEST_CASE("promote from a rows layer writes default.dta byte for byte as from a nested one") {
    auto promoted = [](std::string_view agent) {
        TempDir dir;
        Config cfg({.tweaks = nullptr});
        REQUIRE(cfg.push_layer("default", dir.write("default.dta", std::string(kDefault))));
        REQUIRE(cfg.push_layer("agent", dir.write("agent.dta", std::string(agent))));
        auto r = cfg.promote("agent", "default");
        REQUIRE(r);
        CHECK(r->refused.empty());
        CHECK(r->written.size() == 3);
        return dir.read("default.dta");
    };
    const std::string from_rows = promoted(kAgentRows);
    CHECK(from_rows == promoted(kAgentNested));
    CHECK(from_rows != kDefault);
    CHECK(from_rows.find("(health 1100)") != std::string::npos);
}

TEST_CASE("promote writes a removal: the key's array leaves the target, with its line") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    const std::string base =
        "(enemies\n"
        "   (grunt (health 10))\n"
        "   (exploit_boss (health 9999))   \n"
        "   (bat (health 3)))\n"
        "(menu (debug_menu 1) (volume 3))\n"
        "(cheats\n   (god_mode 1)\n   (noclip 1))\n";
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", base)));
    REQUIRE(cfg.push_layer("agent", dir.write("agent.dta",
                                              "(/enemies.exploit_boss #remove)\n"
                                              "(/menu.debug_menu #remove)\n"
                                              "(/cheats.noclip #remove)\n"
                                              "(/menu.missing #remove)\n")));
    CHECK(!cfg.get<int>("enemies.exploit_boss.health"));
    auto r = cfg.promote("agent", "default");
    REQUIRE(r);
    for (const Error& e : r->refused) INFO(describe(e));
    CHECK(r->refused.empty());
    const std::string after = dir.read("default.dta");
    CHECK(after ==
          "(enemies\n"
          "   (grunt (health 10))\n"
          "   (bat (health 3)))\n"
          "(menu (volume 3))\n"
          "(cheats\n   (god_mode 1)\n   )\n");
    auto reparsed = read_string(after, "default.dta");
    REQUIRE(reparsed);
    CHECK(!(*reparsed)->try_find("enemies")->try_find("exploit_boss"));
    REQUIRE(cfg.remove_layer("agent"));
    CHECK(!cfg.get<int>("enemies.exploit_boss.health"));
    CHECK(cfg.get<int>("enemies.bat.health") == 3);
}

TEST_CASE("data_only refuses a command in a row, at the row's line") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", rows(kDefault, "default.dta")));
    auto r = cfg.check_layer_text("agent.dta",
                                  "(/cpu.easy.reaction 26)\n"
                                  "(/entry.on_hit {play_sfx \"hit.wav\"})\n",
                                  {.data_only = true});
    REQUIRE(r);
    REQUIRE(r->refusals.size() == 1);
    CHECK(r->refusals[0].line() == 2);
    CHECK(r->refusals[0].message().find("'entry.on_hit' holds") == 0);
}

TEST_CASE("the fuzz harness's invariants hold over rows: spans, cache, printing") {
    const char* inputs[] = {
        "(/a.b.c 1)",
        "(/a.b 1 (range 0 2))\n(/a.c \"x\")\n(a (d 3))\n",
        "(/menu.'v1.2'.price 12)\n(/menu.'bacon/eggs' 3)",
        // Not #remove: the harness reads each literal span alone, and #remove alone is an
        // error, so it fails on (m (d #remove)) without rows too.
        "(/m.d 1)\n(/m.e.f {g 1} [p] $v)",
        "(/a 1)\n(/a.b 2)\n(/a.c.d 3)\n(/a.c.e 4)\n(z (y 1))\n(/z.x 2)",
        "  (/k.j.i.h.g.f.e.d.c.b.a 1)  ; deep\n",
        "(/x.y 1)\n(/x.y 2)\n",          // an error: checked for its location
        "(a (/b 1))",                    // an error
        "(/a..b 1) (/a.'b 1) (/a.0 1)",  // errors
        "(/ 1)\n('/x' 1)\n(a /b /c.d)",
    };
    for (const char* in : inputs) {
        INFO(std::string(in));
        CHECK(fuzz::check_parse(in));
    }
}

TEST_CASE("read_structure marks a row's key") {
    const TextStructure s = read_structure("(/a.b 1)\n(c (/d 2))\n('/e' 3)\n(/ 4)");
    std::vector<TextTokenKind> kinds;
    for (const TextBracket& b : s.brackets)
        kinds.push_back(b.tag >= 0 ? s.tokens[static_cast<size_t>(b.tag)].kind
                                   : TextTokenKind::Open);
    CHECK(kinds == std::vector<TextTokenKind>{TextTokenKind::PathKey, TextTokenKind::Symbol,
                                              TextTokenKind::Symbol, TextTokenKind::QuotedSymbol,
                                              TextTokenKind::Symbol});
}

TEST_CASE("the core's rows printer: one row a leaf, metadata in the row, round trip") {
    const auto tree = read_string(
        "(fighters (kestrel (health 1100 (range 500 2000)) (moves (jab (startup 5)))))\n"
        "(menu (debug_menu #remove) ('v1.2' (price 12)))\n(cpu 1 (easy (reaction 26)))\n",
        "a.dta");
    REQUIRE(tree);
    const std::string text = to_dta(**tree, {.indent = 3, .width = 96, .rows = true});
    CHECK(text ==
          "(/fighters.kestrel.health 1100 (range 500 2000))\n"
          "(/fighters.kestrel.moves.jab.startup 5)\n"
          "(/menu.debug_menu #remove)\n"
          "(/menu.'v1.2'.price 12)\n"
          "(/cpu 1)\n"
          "(/cpu.easy.reaction 26)\n");
    auto back = read_string(text, "a.dta");
    REQUIRE(back);
    CHECK(first_difference(**tree, **back).empty());
    // A '/'-led symbol heading an array is quoted, so it reads back as the symbol.
    CHECK(to_dta(**read_string("('/x' 1)\n(a ('/y' 2) /z)", "b.dta")) ==
          "('/x' 1)\n(a ('/y' 2) /z)\n");
}

#endif
