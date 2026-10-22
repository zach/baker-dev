// The experiment's tests: the corpus round trip, the textual round trip of the migrated
// corpus, one case per new form, and every error the reader reports.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#if BAKER_SYNTAX_SHAPE
#include <baker/shape.hpp>
#endif

#include <doctest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace baker;
using namespace baker::experiments;

namespace {

Ref<Array> ok(Expected<Ref<Array>> r) {
    REQUIRE_MESSAGE(bool(r), (r ? std::string() : to_string(r.error())));
    return *r;
}

// The DTA tree `dta` reads to, for comparing with what the new form reads to.
Ref<Array> dta(std::string_view text) {
    return ok(read_string(text, "expected.dta"));
}

std::string message(const Error& e) {
    return std::string(e.message());
}

}  // namespace

TEST_CASE("every corpus file survives the round trip") {
    const auto failures = round_trip_corpus(syntax::print, syntax::read);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        // A file Baker cannot read alone is not the experiment's fault; the rest are.
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
}

TEST_CASE("the migrated corpus reads to the same trees, packs, origins and macros") {
    const std::string root(repository_root());
    int packs = 0, origins = 0, sets = 0, shapes = 0, bodies = 0, read = 0;
    for (const std::string& rel : corpus_files()) {
        const std::string path = root + "/" + rel;
        LoadSession plain;
        plain.set_pack_levels({"engine", "game", "task", "live"});
        auto original = read_file(path, plain);
        if (!original) continue;
        ++read;
        LoadSession migrated;
        migrated.set_pack_levels({"engine", "game", "task", "live"});
        std::vector<syntax::Migration> reports;
        syntax::serve_migrated(migrated, &reports);
        auto back = read_file(path, migrated);
        INFO(rel << ": "
                 << (back ? first_difference(**original, **back) : to_string(back.error())));
        REQUIRE(back);
        CHECK(first_difference(**original, **back).empty());
        REQUIRE(plain.packs().size() == migrated.packs().size());
        for (size_t i = 0; i < plain.packs().size(); ++i) {
            CHECK(plain.packs()[i].name == migrated.packs()[i].name);
            CHECK(plain.packs()[i].level == migrated.packs()[i].level);
            CHECK(plain.packs()[i].version == migrated.packs()[i].version);
            CHECK(plain.packs()[i].stage == migrated.packs()[i].stage);
            CHECK(plain.packs()[i].owners.size() == migrated.packs()[i].owners.size());
            CHECK(plain.packs()[i].requires_packs.size() ==
                  migrated.packs()[i].requires_packs.size());
        }
        REQUIRE(plain.origins().size() == migrated.origins().size());
        for (size_t i = 0; i < plain.origins().size(); ++i) {
            CHECK(plain.origins()[i].source == migrated.origins()[i].source);
            CHECK(plain.origins()[i].tool == migrated.origins()[i].tool);
        }
        REQUIRE(plain.macro_definitions().size() == migrated.macro_definitions().size());
        for (size_t i = 0; i < plain.macro_definitions().size(); ++i) {
            CHECK(plain.macro_definitions()[i].name == migrated.macro_definitions()[i].name);
            CHECK(plain.macro_definitions()[i].flag == migrated.macro_definitions()[i].flag);
        }
        // The main file's own text is the first the reader served.
        REQUIRE(!reports.empty());
        packs += reports[0].packs;
        origins += reports[0].origins;
        sets += reports[0].shape_sets;
        shapes += reports[0].shapes;
        bodies += reports[0].define_bodies;
    }
    CHECK(read > 50);
    // What the corpus holds today: the six stacks' packs and the live-ops example's five,
    // two generated files, twelve (shapes ...) containers, and macros.dta's two bodies.
    CHECK(packs == 11);
    CHECK(origins == 2);
    CHECK(sets == 12);
    CHECK(shapes >= 20);
    CHECK(bodies == 2);
}

TEST_CASE("a converted sample reads back to the same tree") {
    auto original = syntax::read(
        "#pack (menu_core (level game))\n"
        "#shape (dish (at menu) (keyed) (price float (range 0 100)) (calories int))\n"
        "(menu ('bacon and eggs' (price 12.75) (calories 120)))\n"
        "(rules {func scaled ($base) {* $base $difficulty}})\n"
        "(hashes # #1 #00aa00 (key #remove))\n",
        "sample.dta");
    REQUIRE(original);
    const std::string converted = syntax::print(**original);
    auto back = syntax::read(converted, "sample.new");
    REQUIRE(back);
    CHECK(first_difference(**original, **back).empty());
}

TEST_CASE("an error is located") {
    auto r = syntax::read("(menu\n   (price 12.75)\n", "broken.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "broken.dta");
    CHECK(r.error().line() == 1);
}

TEST_CASE("#pack is a directive: anywhere at the top level, once per file") {
    LoadSession session = syntax::corpus_session();
    auto root = ok(syntax::read_with(
        "(duel (hit_pause 6))\n"
        "#pack (duel_core (level game) (owners \"gameplay-fight\" td-combat) (stage custom))\n"
        "(fighters (kestrel (health 1000)))\n",
        "after.dta", session));
    // The declaration is a session record, never a node.
    CHECK(
        first_difference(*root, *dta("(duel (hit_pause 6))\n(fighters (kestrel (health 1000)))\n"))
            .empty());
    REQUIRE(session.packs().size() == 1);
    const Pack& p = session.packs()[0];
    CHECK(p.name.str() == "duel_core");
    CHECK(p.level.str() == "game");
    CHECK(p.stage.str() == "custom");
    REQUIRE(p.owners.size() == 2);
    CHECK(p.owners[1].str() == "td-combat");

    auto twice =
        syntax::read("#pack (a (level game))\n(x 1)\n   #pack (b (level game))\n", "twice.dta");
    REQUIRE(!twice);
    CHECK(twice.error().line() == 3);
    CHECK(twice.error().column() == 4);
    CHECK(message(twice.error()).find("second #pack") != std::string::npos);

    auto nested = syntax::read("(x #pack (a (level game)))\n", "nested.dta");
    REQUIRE(!nested);
    CHECK(message(nested.error()).find("top level") != std::string::npos);

    auto bare = syntax::read("#pack duel_core\n", "bare.dta");
    REQUIRE(!bare);
    CHECK(message(bare.error()).find("#pack (NAME (level L) ...)") != std::string::npos);

    auto open = syntax::read("#pack (duel_core (level game)\n(x 1)\n", "open.dta");
    REQUIRE(!open);
    CHECK(message(open.error()).find("never closed") != std::string::npos);
}

TEST_CASE("#origin is a directive; (origin 0 0 0) and (pack x) are data") {
    LoadSession session = syntax::corpus_session();
    auto root =
        ok(syntax::read_with("(spawn (origin 0 0 0))\n"
                             "#origin (\"economy.xlsx\" (tool sheets-export))\n"
                             "(pack x)\n",
                             "economy.dta", session));
    CHECK(first_difference(*root, *dta("(spawn (origin 0 0 0))\n(pack x)\n")).empty());
    REQUIRE(session.origins().size() == 1);
    CHECK(session.origins()[0].source.str() == "economy.xlsx");
    CHECK(session.origins()[0].tool.str() == "sheets-export");
    CHECK(session.packs().empty());

    auto symbol = syntax::read("#origin (economy (tool x))\n", "sym.dta");
    REQUIRE(!symbol);
    CHECK(message(symbol.error()).find("takes a string first") != std::string::npos);

    auto twice = syntax::read("#origin (\"a\")\n#origin (\"b\")\n", "twice.dta");
    REQUIRE(!twice);
    CHECK(message(twice.error()).find("second #origin") != std::string::npos);

    // Today's forms are read as Baker reads them, each with a note `check` would print.
    std::vector<std::string> notes;
    LoadSession legacy = syntax::corpus_session();
    ok(syntax::read_with("(pack old (level game))\n(shapes (a (x int)))\n", "old.dta", legacy,
                         &notes));
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].find("old.dta:1: a (pack ...) first form: write #pack") != std::string::npos);
    CHECK(notes[1].find("old.dta:2: a (shapes ...) container: write #shape") != std::string::npos);
    CHECK(legacy.packs().size() == 1);
}

TEST_CASE("a #shape run at the top level is one (shapes ...) container") {
    auto root = ok(syntax::read(
        "#shape (fighter (at fighters) (keyed)\n"
        "   (health int (range 1 100000))\n"
        "   (moves any))\n"
        "; a comment between two shapes keeps them in one run\n"
        "#shape (move (at fighters * moves) (keyed) (startup int) (hitbox (list float 4)))\n"
        "(fighters (kestrel (health 1000)))\n"
        "#shape (bot (at bots) (keyed) (block float))\n",
        "shapes.dta"));
    CHECK(first_difference(*root, *dta("(shapes\n"
                                       "   (fighter (at fighters) (keyed)\n"
                                       "      (health int (range 1 100000)) (moves any))\n"
                                       "   (move (at fighters * moves) (keyed) (startup int)"
                                       " (hitbox (list float 4))))\n"
                                       "(fighters (kestrel (health 1000)))\n"
                                       "(shapes (bot (at bots) (keyed) (block float)))\n"))
              .empty());
    // Printed, the container is a run again.
    const std::string printed = syntax::print(*root);
    CHECK(printed.find("#shape (fighter (at fighters) (keyed) (health int (range 1 100000)) "
                       "(moves any))\n#shape (move") != std::string::npos);
    CHECK(printed.find("(shapes") == std::string::npos);
    CHECK(first_difference(*root, *ok(syntax::read(printed, "printed.dta"))).empty());

    auto bare = syntax::read("#shape fighter\n", "bare.dta");
    REQUIRE(!bare);
    CHECK(message(bare.error()).find("#shape takes one form") != std::string::npos);
}

TEST_CASE("a #shape written at its table governs that table's children") {
    const std::string_view text =
        "(fighters\n"
        "   #shape (fighter (health int (range 1 100000)) (width float (range 0.2 3)))\n"
        "   (kestrel (health 1000) (width 0.6))\n"
        "   (rook (health \"lots\") (width 0.5) (armor 1)))\n"
        "(cpu #shape (level (reaction int)) (easy (reaction 30)))\n";
    auto root = ok(syntax::read(text, "default.dta"));
    // The data is untouched; the shape is hoisted with the table's path and (keyed).
    CHECK(first_difference(*root, *dta("(fighters\n"
                                       "   (kestrel (health 1000) (width 0.6))\n"
                                       "   (rook (health \"lots\") (width 0.5) (armor 1)))\n"
                                       "(cpu (easy (reaction 30)))\n"
                                       "(shapes\n"
                                       "   (fighter (at fighters) (keyed) (health int (range 1 "
                                       "100000)) (width float (range 0.2 3)))\n"
                                       "   (level (at cpu) (keyed) (reaction int)))\n"))
              .empty());
#if BAKER_SYNTAX_SHAPE
    shape::Set set;
    shape::Report report;
    set.add(*root, report);
    REQUIRE(set.shapes().size() == 2);
    CHECK(set.find(Symbol("fighter"))->keyed);
    REQUIRE(set.find(Symbol("fighter"))->at.size() == 1);
    CHECK(set.find(Symbol("fighter"))->at[0].str() == "fighters");
    shape::check(*root, set, report);
    REQUIRE(report.errors.size() == 1);
    CHECK(message(report.errors[0]).find("'health' is string, not int") != std::string::npos);
    REQUIRE(report.warnings.size() == 1);  // armor: a field the shape does not know
    CHECK(message(report.warnings[0]).find("armor") != std::string::npos);
#endif

    auto at = syntax::read("(fighters #shape (fighter (at fighters) (health int)))\n", "at.dta");
    REQUIRE(!at);
    CHECK(at.error().line() == 1);
    CHECK(at.error().column() == 27);
    CHECK(message(at.error()).find("(at ...) is not written inside") != std::string::npos);

    auto untagged = syntax::read("(1 2 #shape (x (a int)))\n", "untagged.dta");
    REQUIRE(!untagged);
    CHECK(message(untagged.error()).find("needs tagged ancestors") != std::string::npos);

    auto command = syntax::read("{run #shape (x (a int))}\n", "command.dta");
    REQUIRE(!command);
    CHECK(message(command.error()).find("inside a (...) array") != std::string::npos);

    auto nameless = syntax::read("(fighters #shape ((health int)))\n", "nameless.dta");
    REQUIRE(!nameless);
    CHECK(message(nameless.error()).find("needs a name") != std::string::npos);
}

TEST_CASE("#define NAME is a flag; a macro is [NAME ...]") {
    auto body = syntax::read("[kMin 1]\n#define kMax (4)\n", "macros.dta");
    REQUIRE(!body);
    CHECK(body.error().line() == 2);
    CHECK(body.error().column() == 14);
    CHECK(message(body.error()) == "#define kMax defines a flag; a macro is written [kMax ...]");

    LoadSession session = syntax::corpus_session();
    auto root = ok(syntax::read_with("#define kMax\n(4)\n[kMin 1]\n(limits kMin kMax)\n",
                                     "flags.dta", session));
    CHECK(session.is_defined(Symbol("kMax")));
    CHECK(session.macro(Symbol("kMax")) == nullptr);
    CHECK(first_difference(*root, *dta("(4)\n(limits 1 kMax)\n")).empty());
}

TEST_CASE("# before anything but a letter is a symbol, bare in print and in read") {
    auto root = ok(
        syntax::read("(hashes # #1 #00aa00 #$%& '#remove' '#if' (key #remove))\n", "hashes.dta"));
    const Array& a = *(*root)[0].array();
    REQUIRE(a.size() == 8);
    CHECK(a[1].try_sym()->str() == "#");
    CHECK(a[2].try_sym()->str() == "#1");
    CHECK(a[3].try_sym()->str() == "#00aa00");
    CHECK(a[4].try_sym()->str() == "#$%&");
    CHECK(a[5].try_sym()->str() == "#remove");
    const std::string printed = syntax::print(*root);
    CHECK(printed == "(hashes # #1 #00aa00 #$%& '#remove' '#if' (key #remove))\n");
    // Today's printer quotes them all but the marker.
    CHECK(to_dta(*root) == "(hashes '#' '#1' '#00aa00' '#$%&' '#remove' '#if' (key #remove))\n");
    CHECK(first_difference(*root, *ok(syntax::read(printed, "printed.dta"))).empty());

    auto unknown = syntax::read("(x #foo)\n", "unknown.dta");
    REQUIRE(!unknown);
    CHECK(message(unknown.error()).find("unknown directive '#foo'") != std::string::npos);

    auto quote = syntax::read("(x #1'2)\n", "quote.dta");
    REQUIRE(!quote);
    CHECK(message(quote.error()).find("the prototype cannot read") != std::string::npos);
}

TEST_CASE("the migrator: prefix-and-split, keeping comments and layout") {
    syntax::Migration m;
    const std::string out = syntax::migrate(
        "(pack duel_core (level game) (owners \"gameplay-fight\") (stage custom))\n"
        "; Duel's vocabulary.\n"
        "\n"
        "(shapes\n"
        "   (fighter (at fighters) (keyed)\n"
        "      (health        int   (range 1 100000))\n"
        "      (moves         any))\n"
        "   ; the moves\n"
        "   (move (at fighters * moves) (keyed)\n"
        "      (hitbox    (list float) (desc \"x y w h\"))))\n"
        "(cpu (easy (reaction 30)))\n",
        &m);
    CHECK(out ==
          "#pack (duel_core (level game) (owners \"gameplay-fight\") (stage custom))\n"
          "; Duel's vocabulary.\n"
          "\n"
          "#shape (fighter (at fighters) (keyed)\n"
          "   (health        int   (range 1 100000))\n"
          "   (moves         any))\n"
          "; the moves\n"
          "#shape (move (at fighters * moves) (keyed)\n"
          "   (hitbox    (list float) (desc \"x y w h\")))\n"
          "(cpu (easy (reaction 30)))\n");
    CHECK(m.packs == 1);
    CHECK(m.shape_sets == 1);
    CHECK(m.shapes == 2);

    CHECK(syntax::migrate("(origin \"economy.xlsx\" (tool sheets-export))\n(goods (wheat 2))\n") ==
          "#origin (\"economy.xlsx\" (tool sheets-export))\n(goods (wheat 2))\n");
    CHECK(syntax::migrate("#define kMaxPlayers (4)\n#define HAS_VOCALS\n[FADE (fade 1)]\n") ==
          "[kMaxPlayers 4]\n#define HAS_VOCALS\n[FADE (fade 1)]\n");
    CHECK(syntax::migrate("(shapes (a (x int)) (b (y float)))\n") ==
          "#shape (a (x int))\n#shape (b (y float))\n");
    // Data is left alone: a later (pack ...), an (origin 0 0 0), a nested (shapes ...).
    const std::string_view data = "(spawn (origin 0 0 0))\n(pack x)\n(ui (shapes (a 1)))\n";
    CHECK(syntax::migrate(data) == data);
}

TEST_CASE("nothing in the text crashes the reader: the fuzz regressions and torn text") {
    namespace fs = std::filesystem;
    int files = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(std::string(repository_root())) /
                                                    "tests" / "fuzz_regressions")) {
        if (entry.path().extension() != ".bin") continue;
        auto text = read_text_file(entry.path().string());
        REQUIRE(text);
        auto r = syntax::read(*text, entry.path().filename().string());
        if (r) CHECK(first_difference(**r, *ok(syntax::read(syntax::print(**r), "again"))).empty());
        ++files;
    }
    CHECK(files == 4);
    const std::string_view sample =
        "#pack (a (level game))\n#shape (s (f int))\n(t #shape (u (g float)) (k 1) \"s\n"
        "#define N (4)\n(x #1 #remove 'q) [m 1] {c $v} ; c\n/* b */ #origin (\"o\")\n";
    for (size_t n = 0; n <= sample.size(); ++n) {
        (void)syntax::read(sample.substr(0, n), "torn.dta");
        (void)syntax::migrate(sample.substr(0, n));
    }
}
