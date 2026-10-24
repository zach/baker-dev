// The hash-declarations prototype in the core (dev/syntax-hash-declarations-design.md §2),
// built with BAKER_HASH_DECLARATIONS: #pack, #origin and #shape declare, `#` opens a
// directive only before a letter, and #define NAME (body) names the macro form.
#include <doctest.h>
#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/source_map.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include "ok.hpp"

#include <map>
#include <string>
#include <vector>

#if BAKER_HASH_DECLARATIONS

using namespace baker;
using baker::test::ok;

namespace {

LoadSession session() {
    LoadSession s;
    s.set_pack_levels({"engine", "game", "task"});
    return s;
}

Error error(std::string_view text) {
    LoadSession s = session();
    auto r = read_string(text, "t.dta", s);
    REQUIRE(!r);
    return r.error();
}

std::string message(const Error& e) {
    return std::string(e.message());
}

}  // namespace

TEST_CASE("# before anything but a letter is a symbol, read and printed bare") {
    auto root = ok(read_string("(hashes # #1 #00aa00 #$%& #1'2 '#remove' '#if' (key #remove))\n"));
    const Array& a = *(*root)[0].array();
    REQUIRE(a.size() == 9);
    const char* const names[] = {"hashes", "#", "#1", "#00aa00", "#$%&", "#1'2", "#remove", "#if"};
    for (uint32_t i = 0; i < 8; ++i) CHECK(a[i].try_sym() == Symbol(names[i]));
    CHECK(a[2].try_sym() == Symbol("#1"));  // not a number: the # test comes first
    const std::string printed = to_dta(*root);
    CHECK(printed == "(hashes # #1 #00aa00 #$%& #1'2 '#remove' '#if' (key #remove))\n");
    CHECK(structurally_equal(*root, *ok(read_string(printed))));
    // A letter after # is a directive: an unknown one is an error, as before.
    CHECK(message(error("(x #foo)")) == "unknown directive '#foo'");
    CHECK(message(error("(x #foo-bar)")) == "unknown directive '#foo-bar'");
    // The tolerant read uses the same lexer.
    const TextStructure s = read_structure("#shape (s) #1");
    REQUIRE(s.tokens.size() == 5);
    CHECK(s.tokens[0].kind == TextTokenKind::Directive);
    CHECK(s.tokens[1].kind == TextTokenKind::Open);
    CHECK(s.tokens[4].kind == TextTokenKind::Symbol);
}

TEST_CASE("#pack declares a pack anywhere at a file's top level, once") {
    LoadSession s = session();
    auto root = ok(read_string(
        "(duel (hit_pause 6))\n"
        "#pack (duel_core (level game) (owners \"gameplay-fight\" td-combat) (stage custom))\n"
        "(fighters (kestrel (health 1000)))\n",
        "after.dta", s));
    CHECK(to_string(*root) == "((duel (hit_pause 6)) (fighters (kestrel (health 1000))))");
    REQUIRE(s.packs().size() == 1);
    const Pack& p = s.packs()[0];
    CHECK(p.name == Symbol("duel_core"));
    CHECK(p.level == Symbol("game"));
    CHECK(p.rank == 1);
    CHECK(p.stage == Symbol("custom"));
    REQUIRE(p.owners.size() == 2);
    CHECK(p.owners[1] == Symbol("td-combat"));
    // Macros expand inside a declaration, and a comment may stand between it and its form.
    LoadSession m = session();
    ok(read_string("[LEVEL task]\n#pack ; the pack\n (m (level LEVEL))\n", "m.dta", m));
    REQUIRE(m.packs().size() == 1);
    CHECK(m.packs()[0].level == Symbol("task"));

    const Error twice = error("#pack (a (level game))\n(x 1)\n   #pack (b (level game))\n");
    CHECK(twice.line() == 3);
    CHECK(twice.column() == 4);
    CHECK(message(twice) == "second #pack in this file: a file declares one pack");
    CHECK(message(error("(x #pack (a (level game)))")) == "#pack is written at a file's top level");
    CHECK(message(error("[m #pack (a (level game))]")) == "#pack is written at a file's top level");
    const Error bare = error("#pack duel_core\n");
    CHECK(message(bare) == "#pack takes one form: #pack (NAME (level L) ...)");
    CHECK(bare.column() == 7);
    CHECK(message(error("#pack (duel_core (level game)\n(x 1)\n")) == "'(' is never closed");
    CHECK(message(error("#pack ()")) == "(pack ...) needs a name");
    CHECK(message(error("#pack (p)")) == "pack 'p' needs a (level L)");
    // Today's first form is data.
    LoadSession d = session();
    CHECK(to_string(*ok(read_string("(pack old (level game))", "d.dta", d))) ==
          "((pack old (level game)))");
    CHECK(d.packs().empty());
}

TEST_CASE("a pack is its whole file's: a macro defined or used above #pack is the pack's") {
    static const std::map<std::string, std::string> files = {
        {"task.dta", "[kTaskThing 5]\n#define TASK_FLAG\n#pack (task_pack (level task))\n"},
        {"game.dta", "(early kTaskThing)\n#pack (game_pack (level game))\n(x kTaskThing)\n"},
        {"main.dta", "#require \"task.dta\"\n#require \"game.dta\"\n"}};
    LoadSession s = session();
    s.set_reader([](std::string_view p, TextSink out) {
        const auto it = files.find(std::string(p));
        if (it == files.end()) return false;
        out(it->second);
        return true;
    });
    ok(read_file("main.dta", s));
    REQUIRE(s.packs().size() == 2);
    // One warning, at the first use, which is above #pack.
    REQUIRE(s.warnings().size() == 1);
    CHECK(s.warnings()[0].line() == 1);
    CHECK(message(s.warnings()[0]) ==
          "pack 'game_pack' (level game) uses macro 'kTaskThing' from the softer pack "
          "'task_pack' (level task)");
}

TEST_CASE("#origin declares an origin; (origin 0 0 0) and (pack x) are data") {
    LoadSession s = session();
    auto root =
        ok(read_string("(spawn (origin 0 0 0))\n"
                       "#origin (\"economy.xlsx\" (tool sheets-export))\n"
                       "(pack x)\n",
                       "economy.dta", s));
    CHECK(to_string(*root) == "((spawn (origin 0 0 0)) (pack x))");
    REQUIRE(s.origins().size() == 1);
    CHECK(s.origins()[0].file == Symbol("economy.dta"));
    CHECK(s.origins()[0].source == Symbol("economy.xlsx"));
    CHECK(s.origins()[0].tool == Symbol("sheets-export"));
    CHECK(s.packs().empty());
    CHECK(message(error("#origin (economy (tool x))")) ==
          "#origin takes a string first: #origin (\"source\" (tool NAME))");
    CHECK(message(error("#origin (\"a\")\n#origin (\"b\")")) ==
          "second #origin in this file: a file declares one origin");
    LoadSession d = session();
    ok(read_string("(origin \"x.xlsx\")\n", "d.dta", d));
    CHECK(d.origins().empty());
}

TEST_CASE("a declaration counts only in the branch read") {
    const char* text = "#ifdef A #pack (a (level game)) #else #pack (b (level game)) #endif\n(x 1)";
    LoadSession s = session();
    ok(read_string(text, "c.dta", s));
    REQUIRE(s.packs().size() == 1);
    CHECK(s.packs()[0].name == Symbol("b"));
    LoadSession a = session();
    a.define(Symbol("A"));
    ok(read_string(text, "c.dta", a));
    REQUIRE(a.packs().size() == 1);
    CHECK(a.packs()[0].name == Symbol("a"));
}

TEST_CASE("#define NAME is a flag; a macro is written [NAME ...]") {
    const Error body = error("[kMin 1]\n#define kMax (4)\n");
    CHECK(body.line() == 2);
    CHECK(body.column() == 14);
    CHECK(body.code() == ErrorCode::Directive);
    CHECK(message(body) == "#define kMax defines a flag; a macro is written [kMax ...]");
    LoadSession s = session();
    auto root = ok(read_string("#define kMax\n(4)\n[kMin 1]\n(limits kMin kMax)\n", "f.dta", s));
    CHECK(s.is_defined(Symbol("kMax")));
    CHECK(s.macro(Symbol("kMax")) == nullptr);
    CHECK(to_string(*root) == "((4) (limits 1 kMax))");
}

TEST_CASE("#shape records its form in the session, never in the tree") {
    LoadSession s = session();
    SourceMap map;
    s.source_map = &map;  // edit mode reads it the same
    auto root =
        ok(read_string("(fighters (kestrel (health 1000)))\n"
                       "#shape (fighter (at fighters) (keyed)\n"
                       "   (health int (range 1 100000)))\n"
                       "#shape (move (hitbox (list float 4)))\n",
                       "duel.dta", s));
    CHECK(to_string(*root) == "((fighters (kestrel (health 1000))))");
    REQUIRE(s.shapes().size() == 2);
    const ShapeDecl& f = s.shapes()[0];
    CHECK(f.file == Symbol("duel.dta"));
    CHECK(f.line == 2);
    CHECK(f.root == Symbol("duel.dta"));
    CHECK(to_string(*f.form) == "(fighter (at fighters) (keyed) (health int (range 1 100000)))");
    CHECK(f.form->try_find("health")->line() == 3);
    CHECK(s.shapes()[1].form->tag() == Symbol("move"));
    CHECK(message(error("(fighters #shape (fighter (health int)))")) ==
          "#shape is written at a file's top level");
    CHECK(message(error("{run #shape (x (a int))}")) == "#shape is written at a file's top level");
    CHECK(message(error("#shape fighter")) ==
          "#shape takes one form: #shape (NAME (FIELD TYPE ...) ...)");
    // An included file's shapes are its own, with the read's root.
    LoadSession inc = session();
    inc.set_reader([](std::string_view p, TextSink out) {
        if (p != "pack.dta") return false;
        out("#pack (p (level game))\n#shape (s (a int))\n");
        return true;
    });
    ok(read_string("(x 1)\n#include pack.dta\n", "main.dta", inc));
    REQUIRE(inc.shapes().size() == 1);
    CHECK(inc.shapes()[0].file == Symbol("pack.dta"));
    CHECK(inc.shapes()[0].root == Symbol("main.dta"));
}

TEST_CASE("a cache replays the declarations: pack, origin and shapes (version 7)") {
    static const std::map<std::string, std::string> files = {
        {"main.dta",
         "#origin (\"sheet.xlsx\")\n(a 1)\n#pack (p (level game))\n#require pack.dta\n"},
        {"pack.dta", "#shape (s (a int (range 0 9)))\n#shape (t (b float))\n"}};
    LoadSession bake = session();
    bake.set_reader([](std::string_view p, TextSink out) {
        const auto it = files.find(std::string(p));
        if (it == files.end()) return false;
        out(it->second);
        return true;
    });
    bake.record_for_cache = true;
    auto root = ok(read_file("main.dta", bake));
    std::string bytes;
    REQUIRE(write_cache(*root, string_out(bytes), &bake));
    LoadSession game = session();
    auto back = ok(read_cache_bytes(bytes, "main.datb", game));
    CHECK(structurally_equal(*root, *back));
    REQUIRE(game.packs().size() == 1);
    CHECK(game.packs()[0].name == Symbol("p"));
    REQUIRE(game.origins().size() == 1);
    CHECK(game.origins()[0].source == Symbol("sheet.xlsx"));
    REQUIRE(game.shapes().size() == 2);
    CHECK(game.shapes()[0].file == Symbol("pack.dta"));
    CHECK(game.shapes()[0].line == 1);
    CHECK(game.shapes()[0].root == Symbol("main.dta"));
    CHECK(to_string(*game.shapes()[1].form) == "(t (b float))");
    CHECK(game.shapes()[0].form->try_find("a")->line() == 1);
}

TEST_CASE("a received #shape is refused under data_only and scalars_only") {
    Config cfg({.tweaks = nullptr});
    cfg.session().set_pack_levels({"game", "live"});
    REQUIRE(cfg.push_layer_text("default",
                                "#pack (game_data (level game))\n"
                                "#shape (item (at shop) (keyed) (price int))\n"
                                "(shop (sword (price 10)))\n"));
    const LayerPolicy strict{.data_only = true, .scalars_only = true, .existing_only = true};
    auto declared = cfg.check_layer_text(
        "overlay", "#shape (item (price int (range 0 99999)))\n(shop (sword (price 5000)))\n",
        strict);
    REQUIRE(declared);
    REQUIRE(declared->refusals.size() == 1);
    CHECK(declared->refusals[0].line() == 1);
    CHECK(message(declared->refusals[0]) ==
          "#shape 'item' declares a shape; the layer may change data only");
    // A published overlay declares itself with #pack and #origin, which pass.
    auto overlay = cfg.check_layer_text("overlay",
                                        "#pack (double_xp (level live))\n"
                                        "#origin (\"liveops console\" (tool baker-publish))\n"
                                        "(shop (sword (price 15)))\n",
                                        strict);
    REQUIRE(overlay);
    CHECK(overlay->ok());
    // Today's (shapes ...) is data: existing_only refuses its new paths.
    auto legacy = cfg.check_layer_text("overlay", "(shapes (item (price int)))\n", strict);
    REQUIRE(legacy);
    CHECK_FALSE(legacy->ok());
}

#endif
