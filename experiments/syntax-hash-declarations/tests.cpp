// The experiment's tests: the corpus round trip, the textual round trip of the migrated
// corpus, one case per new form, and every error the reader reports.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#if BAKER_SYNTAX_SHAPE
#include <baker/shape.hpp>
#endif

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <random>
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

// Built with BAKER_HASH_DECLARATIONS (the core reads the new form; syntax.hpp's front end is
// the identity), the shapes are the session's, not the tree's: these tests name both.
#if BAKER_HASH_DECLARATIONS
constexpr bool kCore = true;
#else
constexpr bool kCore = false;
#endif

#if BAKER_SYNTAX_SHAPE
void add_shapes(shape::Set& set, const Array& tree, shape::Report& report,
                const LoadSession* session) {
#if BAKER_HASH_DECLARATIONS
    (void)tree;
    REQUIRE(session);
    set.add(*session, report);
#else
    set.add(tree, report, session);
#endif
}
#endif

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

#if !BAKER_HASH_DECLARATIONS  // with the core switch, the CLI's core-check compares two builds
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
        CHECK(digest(**original) == digest(**back));
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
#endif

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

TEST_CASE("a pack is its whole file's: a macro defined before #pack is the pack's") {
    // The one-way rule (D82): a game pack using a task pack's macro is warned about. Today a
    // macro defined before (pack ...) belongs to no pack; #pack declares the whole file.
    static const std::map<std::string, std::string> files = {
        {"task.dta", "[kTaskThing 5]\n#define TASK_FLAG\n#pack (task_pack (level task))\n"},
        {"game.dta", "#pack (game_pack (level game))\n(x kTaskThing)\n"},
        {"main.dta", "#require \"task.dta\"\n#require \"game.dta\"\n"}};
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task"});
    session.set_reader([](std::string_view p, TextSink out) {
        const auto it = files.find(std::string(p));
        if (it == files.end()) return false;
        auto dta = syntax::translate(it->second, p);
        if (!dta) return false;
        out(*dta);
        return true;
    });
    auto root = ok(read_file("main.dta", session));
    REQUIRE(session.packs().size() == 2);
    REQUIRE(session.warnings().size() == 1);
    CHECK(message(session.warnings()[0])
              .find("uses macro 'kTaskThing' from the softer pack "
                    "'task_pack'") != std::string::npos);
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

    // Today's first forms are data, each with the note `check` would print once per layer.
    std::vector<std::string> notes;
    LoadSession legacy = syntax::corpus_session();
    auto old =
        ok(syntax::read_with("(pack old (level game))\n(origin \"x.xlsx\")\n"
                             "(shapes (a (x int)))\n",
                             "old.dta", legacy, &notes));
    // The tree is the text's three forms, as data.
    REQUIRE(old->size() == 3);
    CHECK((*old)[0].array()->line() == 1);
    CHECK((*old)[0].array()->tag() == Symbol("pack"));
    CHECK((*old)[1].array()->tag() == Symbol("origin"));
    CHECK(legacy.packs().empty());
    CHECK(legacy.origins().empty());
    // The notes are the translator's; the core's check warns instead (reference.cpp).
    CHECK(notes.size() == (kCore ? 0 : 3));
    if (!kCore && notes.size() == 3) {
        CHECK(notes[0].find("old.dta:1: a (pack ...) first form, read as data: write #pack") !=
              std::string::npos);
        CHECK(notes[1].find("old.dta:2: an (origin \"...\") first form, read as data") !=
              std::string::npos);
        CHECK(notes[2].find("old.dta:3: a (shapes ...) container: write #shape") !=
              std::string::npos);
    }
    // The node that keeps the first form data never shows, on its line or in the tree.
    notes.clear();
    auto origin_first = ok(syntax::read_with(
        "#pack (p (level game))\n(origin \"y.xlsx\" (tool t))\n", "first.dta", legacy, &notes));
    REQUIRE(origin_first->size() == 1);
    CHECK((*origin_first)[0].array()->line() == 2);
    CHECK(legacy.origins().empty());
    REQUIRE(legacy.packs().size() == 1);
    if (kCore) return;
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].find("first.dta:2: an (origin \"...\") first form, read as data") !=
          std::string::npos);
}

#if BAKER_HASH_DECLARATIONS
TEST_CASE("a top-level #shape is a session record, as a (shapes ...) child was") {
    LoadSession session = syntax::corpus_session();
    auto root = ok(syntax::read_with(
        "#shape (fighter (at fighters) (keyed)\n"
        "   (health int (range 1 100000))\n"
        "   (moves any))\n"
        "; a comment between two shapes\n"
        "#shape (move (at fighters * moves) (keyed) (startup int) (hitbox (list float 4)))\n"
        "(fighters (kestrel (health 1000)))\n"
        "#shape (bot (at bots) (keyed) (block float))\n",
        "shapes.dta", session));
    CHECK(first_difference(*root, *dta("(fighters (kestrel (health 1000)))\n")).empty());
    REQUIRE(session.shapes().size() == 3);
    CHECK(session.shapes()[0].line == 1);
    CHECK(session.shapes()[1].line == 5);
    CHECK(session.shapes()[2].line == 7);
    CHECK(first_difference(*session.shapes()[1].form,
                           *(*dta("(move (at fighters * moves) (keyed) (startup int)"
                                  " (hitbox (list float 4)))"))[0]
                                .array())
              .empty());
    // Printed, the tree holds no declaration, as a tree never held a pack.
    CHECK(syntax::print(*root) == "(fighters (kestrel (health 1000)))\n");
    auto bare = syntax::read("#shape fighter\n", "bare.dta");
    REQUIRE(!bare);
    CHECK(message(bare.error()).find("#shape takes one form") != std::string::npos);
}
#else
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
#endif

#if BAKER_HASH_DECLARATIONS
TEST_CASE("a #shape at its table is the translator's only: the core refuses it, located") {
    auto r = syntax::read("(fighters\n   #shape (fighter (health int)))\n", "table.dta");
    REQUIRE(!r);
    CHECK(r.error().line() == 2);
    CHECK(r.error().column() == 4);
    CHECK(message(r.error()) == "#shape is written at a file's top level");
}
#else
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

    auto second = syntax::read(
        "(fighters #shape (fighter (health int))\n   #shape (other (width float)))\n", "two.dta");
    REQUIRE(!second);
    CHECK(second.error().line() == 2);
    CHECK(second.error().column() == 4);
    CHECK(message(second.error()).find("a second #shape at one table") != std::string::npos);

    auto nameless = syntax::read("(fighters #shape ((health int)))\n", "nameless.dta");
    REQUIRE(!nameless);
    CHECK(message(nameless.error()).find("needs a name") != std::string::npos);
}
#endif

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
    // Today's printer quotes them all but the marker; the core's, with the switch, as written.
    CHECK(to_dta(*root) ==
          (kCore ? "(hashes # #1 #00aa00 #$%& '#remove' '#if' (key #remove))\n"
                 : "(hashes '#' '#1' '#00aa00' '#$%&' '#remove' '#if' (key #remove))\n"));
    CHECK(first_difference(*root, *ok(syntax::read(printed, "printed.dta"))).empty());

    auto unknown = syntax::read("(x #foo)\n", "unknown.dta");
    REQUIRE(!unknown);
    CHECK(message(unknown.error()).find("unknown directive '#foo'") != std::string::npos);

    auto quote = syntax::read("(x #1'2)\n", "quote.dta");
    if (kCore) {  // the translator's limit: the core reads it, and prints it bare
        REQUIRE(quote);
        CHECK(syntax::print(**quote) == "(x #1'2)\n");
    } else {
        REQUIRE(!quote);
        CHECK(message(quote.error()).find("the prototype cannot read") != std::string::npos);
    }
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

TEST_CASE("nothing in the text crashes the reader: random token soup") {
    // A fixed seed: the same 3,000 texts every run, of the tokens the new forms are made of.
    static const char* const tokens[] = {"#pack",
                                         "#origin",
                                         "#shape",
                                         "#define",
                                         "#ifdef X",
                                         "#ifndef X",
                                         "#else",
                                         "#endif",
                                         "#include x.dta",
                                         "#require \"y.dta\"",
                                         "#autorun",
                                         "#remove",
                                         "#1",
                                         "#",
                                         "#$%&",
                                         "#foo",
                                         "(",
                                         ")",
                                         "[",
                                         "]",
                                         "{",
                                         "}",
                                         "\"s\"",
                                         "'q'",
                                         "'",
                                         "\"",
                                         "pack",
                                         "origin",
                                         "shapes",
                                         "(at",
                                         "(keyed)",
                                         "(list float 4)",
                                         "x",
                                         "1",
                                         "-2.5",
                                         "$v",
                                         "; c\n",
                                         "/* b */",
                                         "\n",
                                         " ",
                                         "\xEF\xBB\xBF",
                                         "(level game)"};
    std::mt19937 rng(20261024);
    std::uniform_int_distribution<size_t> pick(0, std::size(tokens) - 1), length(1, 40);
    for (int i = 0; i < 3000; ++i) {
        std::string text;
        for (size_t n = length(rng); n > 0; --n) {
            text += tokens[pick(rng)];
            text += ' ';
        }
        auto r = syntax::read(text, "soup.dta");
        if (r) {
            // Whatever reads prints, and reads back to the same tree.
            auto back = syntax::read(syntax::print(**r), "soup.new");
            INFO(text);
            REQUIRE(back);
            CHECK(first_difference(**r, **back).empty());
        }
        (void)syntax::migrate(text);
    }
}

TEST_CASE("every sample reads in the new form") {
    namespace fs = std::filesystem;
    const fs::path samples = fs::path(std::string(repository_root())) / "experiments" /
                             "syntax-hash-declarations" / "samples";
    int read = 0;
    for (const auto& entry : fs::recursive_directory_iterator(samples)) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() != ".dta" || name.find(".before.") != std::string::npos)
            continue;
        // The at-the-table #shape is the translator's only (design note §9, question 1).
        if (kCore && entry.path().string().ends_with("duel-at-table/data/default.dta")) continue;
        LoadSession session = syntax::corpus_session();
        auto text = read_text_file(entry.path().string());
        REQUIRE(text);
        INFO(entry.path().string());
        ok(syntax::read_with(*text, entry.path().string(), session));
        ++read;
    }
    CHECK(read == (kCore ? 14 : 15));
}

#if BAKER_SYNTAX_SHAPE
namespace {

// A stack's data/default.dta read as the stack reads it, its pack (and Ledger's economy.dta)
// served from the samples, in the new form, when `migrated`: the shapes it declares, checked
// over its data.
struct StackRun {
    shape::Set set;
    shape::Report report;
    LoadSession session;
    Ref<Array> tree;
};

void run_stack(const std::string& stack, bool migrated, StackRun& r) {
    const std::string root(repository_root());
    r.session.set_pack_levels({"engine", "game", "task"});
    if (migrated)
        r.session.set_reader([stack, root](std::string_view path, TextSink out) {
            std::string p(path);
            for (const std::string& served : {stack + "_core.dta", std::string("economy.dta")})
                if (p.size() > served.size() && p.ends_with("/" + served))
                    p = root + "/experiments/syntax-hash-declarations/samples/" + served;
            auto text = read_text_file(p);
            if (!text) return false;
            auto dta = syntax::translate(*text, path);
            if (!dta) return false;
            out(*dta);
            return true;
        });
    auto tree = read_file(root + "/stacks/" + stack + "/data/default.dta", r.session);
    REQUIRE_MESSAGE(bool(tree), (tree ? std::string() : to_string(tree.error())));
    r.tree = *tree;
    add_shapes(r.set, **tree, r.report, &r.session);
    shape::check(**tree, r.set, r.report);
}

// The check finding the first design question weighs against the at-the-table #shape: a
// shape that governs no array of the tree, as D108's silent wrong depth does. Each tagged
// array's chain of tags from the root, its own last, against each shape's place.
std::vector<std::string> governs_nothing(const shape::Set& set, const Array& root) {
    std::vector<std::vector<Symbol>> chains;
    std::vector<Symbol> chain;
    auto walk = [&](auto& self, const Array& a) -> void {
        for (const Node& n : a) {
            const Array* c = n.array();
            if (!c) continue;
            const Symbol tag = c->tag();
            if (!tag.empty() && chain.empty() && tag == Symbol("shapes")) continue;
            if (!tag.empty()) {
                chain.push_back(tag);
                chains.push_back(chain);
            }
            self(self, *c);
            if (!tag.empty()) chain.pop_back();
        }
    };
    walk(walk, root);
    std::vector<std::string> found;
    for (const shape::Shape& sh : set.shapes()) {
        const bool any = std::any_of(chains.begin(), chains.end(), [&](const auto& c) {
            if (!sh.keyed && c.back() != sh.name) return false;
            if (sh.at.empty()) return true;
            if (sh.at.size() != c.size() - 1) return false;
            for (size_t i = 0; i < sh.at.size(); ++i)
                if (sh.at[i] != Symbol("*") && sh.at[i] != c[i]) return false;
            return true;
        });
        if (!any)
            found.push_back(std::string(sh.file.str()) + ":" + std::to_string(sh.line) +
                            ": shape '" + std::string(sh.name.str()) + "' governs no array");
    }
    return found;
}

}  // namespace

TEST_CASE("each stack's migrated pack, with its arities, holds over the stack's real data") {
    int arities = 0;
    for (const std::string stack :
         {"contraption", "duel", "hearth", "ledger", "stuntline", "wayfarer"}) {
        INFO(stack);
        StackRun before, after;
        run_stack(stack, true, after);
        CHECK(after.report.errors.empty());
        for (const shape::Shape& sh : after.set.shapes())
            for (const shape::Field& f : sh.fields) arities += f.min_count > 0 ? 1 : 0;
        if (kCore) {
            // Today's pack is data to the core: what the stack declares is the migrated pack's
            // alone (the two-build core-check compares it with today's read).
            CHECK(after.set.shapes().size() >= 2);
            REQUIRE(after.session.packs().size() == 1);
            CHECK(governs_nothing(after.set, *after.tree).empty());
            continue;
        }
        run_stack(stack, false, before);
        CHECK(before.report.errors.empty());
        CHECK(after.report.warnings.size() == before.report.warnings.size());
        REQUIRE(after.set.shapes().size() == before.set.shapes().size());
        // The same packs, from #pack.
        REQUIRE(after.session.packs().size() == before.session.packs().size());
        CHECK(after.session.packs()[0].name == before.session.packs()[0].name);
        CHECK(after.session.packs()[0].owners.size() == before.session.packs()[0].owners.size());
        CHECK(after.session.origins().size() == before.session.origins().size());
        // The check finding has nothing to say about the stacks as they are.
        for (const std::string& f : governs_nothing(before.set, *before.tree)) INFO(f);
        CHECK(governs_nothing(before.set, *before.tree).empty());
    }
    CHECK(arities == 11);  // of the six packs' 15 list fields; the other four are open lists
}

#if !BAKER_HASH_DECLARATIONS  // the at-the-table #shape is the translator's only
TEST_CASE("Duel's shapes at their tables, and four floats are four") {
    const std::string root(repository_root());
    const std::string dir = root + "/experiments/syntax-hash-declarations/samples/duel-at-table";
    LoadSession session = syntax::corpus_session();
    auto text = read_text_file(dir + "/data/default.dta");
    REQUIRE(text);
    auto duel = ok(syntax::read_with(*text, dir + "/data/default.dta", session));
    shape::Set set;
    shape::Report report;
    set.add(*duel, report, &session);
    REQUIRE(set.shapes().size() == 4);
    for (const auto& [name, at] : {std::pair<const char*, const char*>{"fighter", "fighters"},
                                   {"cpu_level", "cpu"},
                                   {"bot", "bots"}}) {
        const shape::Shape* sh = set.find(Symbol(name));
        REQUIRE(sh);
        CHECK(sh->keyed);
        REQUIRE(sh->at.size() == 1);
        CHECK(sh->at[0].str() == at);
        // Located where it is written, at its table.
        CHECK(sh->file.str().ends_with("duel-at-table/data/default.dta"));
    }
    CHECK(set.find(Symbol("fighter"))->line == 22);
    CHECK(set.find(Symbol("fighter"))->field(Symbol("health"))->line == 23);
    CHECK(set.find(Symbol("cpu_level"))->line == 52);
    CHECK(set.find(Symbol("cpu_level"))->field(Symbol("reaction"))->line == 53);
    CHECK(set.find(Symbol("bot"))->line == 68);
    const shape::Shape* move = set.find(Symbol("move"));
    REQUIRE(move);
    CHECK(move->file.str().ends_with("packs/game/duel_core.dta"));
    CHECK(move->field(Symbol("hitbox"))->min_count == 4);
    CHECK(move->field(Symbol("hitbox"))->max_count == 4);
    shape::check(*duel, set, report);
    CHECK(report.errors.empty());
    CHECK(report.warnings.empty());
    REQUIRE(session.packs().size() == 1);
    CHECK(session.packs()[0].name.str() == "duel_core");

    // The fighter shape, at its table, catches what the pack's did: a health of the wrong
    // type. A hitbox with three values, in an otherwise whole fighter, is one error.
    auto torn = ok(syntax::read(
        "(fighters #shape (fighter (health int (range 1 100000)) (moves any))\n"
        "   (kestrel (health \"lots\")\n"
        "      (moves (jab (button light) (startup 4) (active 2) (recovery 7) (damage 30)"
        " (hitstun 12) (blockstun 9) (hitbox 0.3 1.3 0.55)))))\n"
        "#shape (move (at fighters * moves) (keyed) (button symbol) (startup int) (active int)"
        " (recovery int) (damage int) (hitstun int) (blockstun int) (hitbox (list float 4)))\n",
        "torn.dta"));
    shape::Set torn_set;
    shape::Report torn_report;
    torn_set.add(*torn, torn_report);
    shape::check(*torn, torn_set, torn_report);
    REQUIRE(torn_report.errors.size() == 2);
    CHECK(message(torn_report.errors[0]).find("'health' is string, not int") != std::string::npos);
    CHECK(torn_report.errors[0].line() == 2);
    CHECK(message(torn_report.errors[1]).find("'hitbox' holds 3 values, not 4") !=
          std::string::npos);
    CHECK(torn_report.errors[1].line() == 3);
}
#endif

TEST_CASE("a shape at the wrong depth checks nothing, silently; the finding names it") {
    // D108's failure: (at fighters moves) for (at fighters * moves). Every move goes
    // unchecked, a startup of 900 and an undeclared armor included, and nothing says so.
    LoadSession session = syntax::corpus_session();
    auto tree = ok(
        syntax::read_with("#shape (move (at fighters moves) (keyed) (startup int (range 1 60)))\n"
                          "(fighters (kestrel (moves (jab (startup 900) (armor 1)))))\n",
                          "wrong.dta", session));
    shape::Set set;
    shape::Report report;
    add_shapes(set, *tree, report, &session);
    shape::check(*tree, set, report);
    CHECK(report.errors.empty());
    CHECK(report.warnings.empty());
    const auto found = governs_nothing(set, *tree);
    REQUIRE(found.size() == 1);
    CHECK(found[0] == "wrong.dta:1: shape 'move' governs no array");
#if !BAKER_HASH_DECLARATIONS  // the at-the-table #shape is the translator's only
    // Written at its table, it cannot be at the wrong depth: the place is where it stands.
    auto at_table =
        ok(syntax::read("(fighters (kestrel (moves #shape (move (startup int (range 1 60)))"
                        " (jab (startup 900) (armor 1)))))\n",
                        "table.dta"));
    shape::Set table_set;
    shape::Report table_report;
    table_set.add(*at_table, table_report);
    shape::check(*at_table, table_set, table_report);
    CHECK(table_report.errors.size() == 1);
    CHECK(table_report.warnings.size() == 1);
    CHECK(governs_nothing(table_set, *at_table).empty());
#endif
}

TEST_CASE("a softer pack may narrow a #shape, not loosen it (D145 unchanged)") {
    static const std::map<std::string, std::string> files = {
        {"base.dta",
         "#pack (game_rules (level game))\n"
         "#shape (player (speed int (range 1 10)) (pos (list float 2 3)) (name symbol))\n"
         "(player (speed 5) (pos 1 2) (name p))\n"},
        {"loose.dta",
         "(player (speed 900) (pos 1 2 3 4))\n"
         "#pack (fast_mod (level mod))\n"  // after data: a directive, anywhere at the top
         "#shape (player (speed int (range 1 1000)) (pos (list float 1 4))"
         " (name symbol (optional)))\n"},
        {"tight.dta",
         "#pack (tight_mod (level mod))\n"
         "#shape (player (speed int (range 2 8)) (pos (list float 2)))\n"
         "(player (speed 9) (pos 1 2))\n"}};
    struct Run {
        Config cfg{{.tweaks = nullptr}};
        std::vector<std::string> reported;
        explicit Run(std::initializer_list<const char*> layers) {
            cfg.session().set_pack_levels({"engine", "game", "mod"});
            cfg.session().set_reader([](std::string_view p, TextSink out) {
                const auto it = files.find(std::string(p));
                if (it == files.end()) return false;
                auto dta = syntax::translate(it->second, p);
                if (!dta) return false;
                out(*dta);
                return true;
            });
            cfg.set_warning_handler(
                [this](const Error& e) { reported.emplace_back(to_string(e)); });
            cfg.add_validator(shape::validator(), {.any_thread = true});
            for (const char* l : layers) REQUIRE(cfg.push_layer(l, l));
        }
        [[nodiscard]] int count(const std::string& part) const {
            return static_cast<int>(std::count_if(
                reported.begin(), reported.end(),
                [&](const std::string& r) { return r.find(part) != std::string::npos; }));
        }
    };
    // The mod widens the range and the count and makes name optional: three warnings, and
    // the base's declarations hold, so its 900 and its four values are still errors.
    Run loose({"base.dta", "loose.dta"});
    CHECK(loose.count("loose.dta:3") >= 3);
    CHECK(loose.count("'speed' is 900, outside 1.0 to 10.0") >= 1);
    CHECK(loose.count("'pos' holds 4 values, not 2 to 3") >= 1);
    // Narrowing takes effect.
    Run tight({"base.dta", "tight.dta"});
    CHECK(tight.count("'speed' is 9, outside 2.0 to 8.0") >= 1);
    CHECK(tight.count("tight.dta:2") == 0);
}

TEST_CASE("received text: a (shapes ...) loosens a pack's shape today; #shape is refused") {
    Config cfg({.tweaks = nullptr});
    cfg.session().set_pack_levels({"engine", "game", "live"});
    cfg.add_validator(shape::validator(), {.any_thread = true});
    REQUIRE(cfg.push_layer_text(
        "default", *syntax::translate("#pack (game_data (level game) (version \"1.0\"))\n"
                                      "#shape (item (at shop) (keyed) (price int (range 0 1000)))\n"
                                      "(shop (sword (price 10)) (shield (price 20)))\n",
                                      "default")));
    // Every rule but allow: a received layer may change existing values, to plain values.
    const LayerPolicy strict{
        .data_only = true, .scalars_only = true, .existing_only = true, .warnings_refuse = true};
    auto plain = cfg.check_layer_text("overlay", "(shop (sword (price 5000)))\n", strict);
    REQUIRE(plain);
    CHECK_FALSE(plain->ok());  // out of the pack's range
    // Today, an overlay that declares no pack retunes the shape's range as data (two numbers
    // the stack already has), and the 5000 passes. The adopted reader reads it as data that
    // declares nothing, and existing_only refuses its new paths.
    auto loosened = cfg.check_layer_text(
        "overlay", "(shapes (item (price int (range 0 99999))))\n(shop (sword (price 5000)))\n",
        strict);
    REQUIRE(loosened);
    CHECK(loosened->ok() == !kCore);  // with the core switch, (shapes ...) is data: refused
    // In the new form a shape is a declaration, which the policy classifies.
    auto declared = syntax::check_received(
        cfg, "overlay", "#shape (item (price int (range 0 99999)))\n(shop (sword (price 5000)))\n",
        strict);
    if (kCore) {  // the core's refusal is the report's, as every policy refusal is
        REQUIRE(declared);
        REQUIRE(!declared->refusals.empty());
        CHECK(declared->refusals[0].code() == ErrorCode::Refused);
        CHECK(declared->refusals[0].line() == 1);
        CHECK(message(declared->refusals[0]) ==
              "#shape 'item' declares a shape; the layer may change data only");
    } else {
        REQUIRE(!declared);
        CHECK(declared.error().code() == ErrorCode::Refused);
        CHECK(declared.error().line() == 1);
        CHECK(message(declared.error()).find("a received layer read under data_only") !=
              std::string::npos);
    }
    // A published overlay declares itself with #pack and #origin, which pass.
    auto overlay = syntax::check_received(
        cfg, "overlay",
        "#pack (double_xp (level live) (version 1) (requires (game_data \"1.0\")))\n"
        "#origin (\"liveops console, ticket 4412\" (tool baker-publish))\n"
        "(shop (sword (price 15)))\n",
        strict);
    REQUIRE(overlay);
    CHECK(overlay->ok());
    // Without data_only and scalars_only, a received #shape is admitted, and D145 holds it.
    const LayerPolicy open{.warnings_refuse = true};
    auto admitted =
        syntax::check_received(cfg, "overlay",
                               "#pack (o (level live))\n#shape (item (price int (range 0 99999)))\n"
                               "(shop (sword (price 5000)))\n",
                               open);
    REQUIRE(admitted);
    CHECK_FALSE(admitted->ok());
}

TEST_CASE("a cache of a migrated pack replays its pack and keeps its shapes") {
    const std::string root(repository_root());
    const std::string pack =
        root +
        "/experiments/syntax-hash-declarations/samples/duel-at-table/packs/game/"
        "duel_core.dta";
    LoadSession recorded = syntax::corpus_session();
    recorded.record_for_cache = true;
    auto text = read_text_file(pack);
    REQUIRE(text);
    auto tree = ok(syntax::read_with(*text, pack, recorded));
    std::string bytes;
    REQUIRE(write_cache(*tree, string_out(bytes), &recorded));
    LoadSession fresh = syntax::corpus_session();
    auto back = ok(read_cache_bytes(bytes, "duel_core.datb", fresh));
    CHECK(first_difference(*tree, *back).empty());
    REQUIRE(fresh.packs().size() == 1);
    CHECK(fresh.packs()[0].name.str() == "duel_core");
    shape::Set set;
    shape::Report report;
    add_shapes(set, *back, report, &fresh);
    REQUIRE(set.find(Symbol("move")));
    CHECK(set.find(Symbol("move"))->field(Symbol("hitbox"))->min_count == 4);
    CHECK(set.find(Symbol("move"))->line == 9);
}
#endif
