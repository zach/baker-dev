// The indent-lines experiment in the core: a line that
// begins with an atom is an array. Built only when the build reads lines
// (-DBAKER_INDENT_LINES=ALWAYS or DIRECTIVE); empty otherwise.
#include <doctest.h>

#if defined(BAKER_INDENT_LINES) && BAKER_INDENT_LINES

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/source_map.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include "ok.hpp"
#include "temp_dir.hpp"

#include <string>

using namespace baker;
using baker::test::ok;
using baker::test::TempDir;

namespace {

// Text read as lines from the start, in either build.
Expected<Ref<Array>> lines(std::string_view text, LoadSession session = {}) {
    session.lines = true;
    return read_string(text, "t.dta", session);
}
// Text read as today.
Ref<Array> brackets(std::string_view text) {
    LoadSession session;
    session.lines = false;
    return ok(read_string(text, "t.dta", session));
}
void same(std::string_view as_lines, std::string_view as_brackets) {
    CAPTURE(as_lines);
    const auto got = lines(as_lines);
    REQUIRE_MESSAGE(got, (got ? "" : to_string(got.error())));
    CHECK(to_string(Node(*got)) == to_string(Node(brackets(as_brackets))));
}
Error error_of(std::string_view text) {
    auto r = lines(text);
    CHECK_MESSAGE(!r, "read as " << (r ? to_string(Node(*r)) : std::string()));
    return r ? Error() : r.error();
}

}  // namespace

TEST_CASE("lines: the brief's snippets") {
    // examples/quickstart/default.dta
    same("player\n   speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")\n",
         "(player (speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")))");
    // tests/host/stack/data/default.dta, its trailing comment kept
    same(
        "; The test game's data.\nshapes\n   player\n      speed float (range 0 10)\n"
        "      lives int\nplayer\n   speed 3.5   ; how fast\n   lives 3\nwaves {spawn grunt 3}\n",
        "(shapes (player (speed float (range 0 10)) (lives int))) (player (speed 3.5) (lives 3))"
        " (waves {spawn grunt 3})");
    // Hearth's households: tuple rows keep their parentheses, as values
    same(
        "households\n   cottage\n      desc \"Two rooms and a couple\"\n      size 14 10\n"
        "      walls\n         (0 -5 14 0.2) (0 5 14 0.2)\n         (1 -3 0.2 4)\n"
        "      residents\n         ada (weights (social 1.4)) (color 0.95 0.55 0.4)\n",
        "(households (cottage (desc \"Two rooms and a couple\") (size 14 10)"
        " (walls (0 -5 14 0.2) (0 5 14 0.2) (1 -3 0.2 4))"
        " (residents (ada (weights (social 1.4)) (color 0.95 0.55 0.4)))))");
}

TEST_CASE("lines: each form") {
    same("landmark\n", "(landmark)");
    same("'bacon and eggs' (price 12.75)\n", "('bacon and eggs' (price 12.75))");
    same("speed $base\n", "(speed $base)");
    same("waves {spawn grunt 3}\n", "(waves {spawn grunt 3})");
    same("hud [focus_ring color]\n", "(hud [focus_ring color])");
    same("key #remove\n", "(key #remove)");
    same("1 2 3\n", "(1 2 3)");  // any atom opens a line
    // A bracket group spanning lines inside a line: indentation means nothing in it.
    same("hud\n   (menu\n (x 1)\n        (y 2))\n   z 3\n", "(hud (menu (x 1) (y 2)) (z 3))");
    // A string over two lines does not start one; the line after it does.
    same("a\n   desc \"one\ntwo\" 3\n   b 4\n", "(a (desc \"one\ntwo\" 3) (b 4))");
    // Comments, and a dedent by two levels at once.
    same("a ; x\n   ; y\n   b 1 /* z */\n   /* w */\n   c 2\nd\n   e\n      f 1\ng 2\n",
         "(a (b 1) (c 2)) (d (e (f 1))) (g 2)");
    // A block comment before a line's first token counts in its indent.
    same("a\n   b\n   /* w */ c 2\n", "(a (b (c 2)))");
    // A line closes at the end of the file, without a newline too.
    same("a\n   b 1", "(a (b 1))");
    same(
        "\xEF\xBB\xBF"
        "a 1\n",
        "(a 1)");  // after a byte order mark
}

TEST_CASE("lines: a bracketed file reads as today, but for a bare atom first on its line") {
    same("(player\n   (speed 3.5)\n   (lives 3))\n", "(player (speed 3.5) (lives 3))");
    same("(a 1)\n   (b 2)\n(c 3)\n", "(a 1) (b 2) (c 3)");  // brackets' indentation: nothing
    same("(a\n\t(b 1))\n\t(c 2)\n", "(a (b 1)) (c 2)");     // a tab where it decides nothing
    same("(a 1) b 3\n", "(a 1) b 3");  // test_write.cpp:84: b is not first on its line
    // The one way a bracketed text changes: a bare atom first on its line at the top level.
    same("(a 1)\nb 3\n", "(a 1) (b 3)");
    same("42\n", "(42)");
}

TEST_CASE("lines: a value's text reads as today, and #lines turns the layout on") {
    LoadSession value;
    value.lines = false;  // a tweak's default, eval(text), a console request
    CHECK(to_string(Node(ok(read_string("42", "v", value)))) == "(42)");
    CHECK(ok(read_string("42", "v", value))->size() == 1);
    CHECK((*ok(read_string("42", "v", value)))[0].try_int() == 42);
    CHECK(to_string(Node(ok(read_string("#lines\nb 3\n", "v", value)))) == "((b 3))");
    CHECK(to_string(Node(ok(read_string("#lines 5 (a 1)\n", "v", value)))) == "(5 (a 1))");
    const auto inside = read_string("(a\n#lines\n)", "v", value);
    REQUIRE_FALSE(inside);
    CHECK(inside.error().message() == "#lines is written outside brackets");
    CHECK(inside.error().line() == 2);
    if (BAKER_INDENT_LINES == 1) {
        CHECK(to_string(Node(ok(read_string("b 3\n", "t.dta")))) == "((b 3))");
    } else {  // DIRECTIVE: a file reads as today until its #lines
        CHECK(to_string(Node(ok(read_string("b 3\n", "t.dta")))) == "(b 3)");
        CHECK(to_string(Node(ok(read_string("b 3\n#lines\nc 4\n", "t.dta")))) == "(b 3 (c 4))");
    }
}

TEST_CASE("lines: directives") {
    // A conditional takes no part in the layout: whole lines inside a block, at the margin.
    same(
        "player\n   speed 3\n#ifdef NOT_DEFINED_HERE\n   god 1\n#else\n   lives 3\n#endif\n"
        "   jump 2\nwaves 1\n",
        "(player (speed 3) (lives 3) (jump 2)) (waves 1)");
    // ui_menu.dta:39-43: conditionals between whole top-level lines.
    same("#ifdef HAS_VOCALS\nvocals_panel (lanes 3)\n#else\nvocals_panel (lanes 0)\n#endif\n",
         "(vocals_panel (lanes 0))");
    // Any other directive's indent closes lines like a token's, and it opens none.
    same("player\n   #define kSpeed (3)\n   speed kSpeed\n", "(player (speed 3))");
    same("[kDefaults (a 1) (b 2)]\nplayer\n   kDefaults\n   c 3\n", "(player (a 1) (b 2) (c 3))");
    // #include inside a line block splices into it; at the margin, into the root.
    TempDir dir;
    dir.write("part.dta", "(x 1) (y 2)\n");
    const std::string main = dir.write("main.dta",
                                       "#lines\nplayer\n   speed 3\n   #include part.dta\nlast 1\n"
                                       "#include part.dta\n");
    CHECK(to_string(Node(ok(read_file(main)))) ==
          "((player (speed 3) (x 1) (y 2)) (last 1) (x 1) (y 2))");
    // A pack declaration as a line reaches the pack machinery.
    LoadSession packs;
    packs.set_pack_levels({"engine", "game"});
    const auto p = lines("pack core (level game)\nplayer\n   speed 3\n", packs);
    REQUIRE(p);
    CHECK(to_string(Node(*p)) == "((player (speed 3)))");
}

TEST_CASE("lines: errors are located") {
    // A dedent that lands between levels.
    Error e = error_of("player\n   speed 3\n lives 1\n");
    CHECK(e.code() == ErrorCode::Syntax);
    CHECK(e.message() == "indent 1 matches no open line");
    CHECK(e.line() == 3);
    CHECK(e.column() == 2);
    // A top-level line begins at the margin: after a directive closed the block, too.
    e = error_of("player\n   speed 3\n#define FAST\n   lives 3\n");
    CHECK(e.message() == "indent 3 matches no open line");
    CHECK(e.line() == 4);
    // A dedent onto an outer line's indent is a sibling: well-formed (a limit, not an error).
    same("a\n   b\n      c 1\n   d 2\n", "(a (b (c 1)) (d 2))");
    // A tab in a line's indentation.
    e = error_of("player\n\tspeed 3\n");
    CHECK(e.message() == "a tab in a line's indentation: indent with spaces");
    CHECK(e.line() == 2);
    CHECK(e.column() == 2);
    // The continued value list (four sim files: (objectives wayfarer.a / wayfarer.b ...)).
    e = error_of("objectives wayfarer.a\n   wayfarer.b wayfarer.c\n");
    CHECK(e.message() ==
          "a value list continues on the same line or in brackets; a deeper line is a child");
    CHECK(e.line() == 2);
    CHECK(e.column() == 4);
    same("objectives wayfarer.a\n   (wayfarer.b) (wayfarer.c)\n",
         "(objectives wayfarer.a (wayfarer.b) (wayfarer.c))");  // groups continue it
    // A close bracket first on a line where no bracket is open.
    e = error_of("a\n   b 1\n   )\n");
    CHECK(e.message() == "unexpected ')'");
    CHECK(e.line() == 3);
    // #remove stays a key's one value.
    CHECK(error_of("key #remove\n   b 1\n").code() == ErrorCode::Syntax);
    // Deep indentation meets the depth limit.
    LoadSession shallow;
    shallow.limits.max_depth = 2;
    const auto deep = lines("a\n b\n  c\n   d\n", shallow);
    REQUIRE_FALSE(deep);
    CHECK(deep.error().code() == ErrorCode::Limit);
}

TEST_CASE("lines: provenance and edit-mode extents") {
    SourceMap map;
    LoadSession edit;
    edit.source_map = &map;
    const std::string_view text = "player\n   speed 3.5   ; how fast\n   lives 3\nwaves 2\n";
    const Ref<Array> root = ok(lines(text, edit));
    const Array& player = *(*root)[0].array();
    CHECK(player.line() == 1);
    CHECK((*player[1].array()).line() == 2);
    const SourceSpan* whole = map.extent(player);
    REQUIRE(whole);
    CHECK(text.substr(whole->begin, whole->end - whole->begin) ==
          "player\n   speed 3.5   ; how fast\n   lives 3");
    const SourceSpan* speed = map.span(*player[1].array(), 1);
    REQUIRE(speed);
    CHECK(text.substr(speed->begin, speed->end - speed->begin) == "3.5");
}

TEST_CASE("lines: the printer") {
    WriteOptions o;
    o.lines = true;
    const auto print = [&](std::string_view text) {
        std::string s = to_dta(*brackets(text), o);
        if (BAKER_INDENT_LINES == 2) {  // the opt-in build writes its directive first
            CHECK(s.rfind("#lines\n", 0) == 0);
            s.erase(0, 7);
        }
        return s;
    };
    CHECK(print("(player (speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")))") ==
          "player\n   speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")\n");
    CHECK(print("(items a b c) (waves {spawn grunt 3})") == "items a b c\nwaves {spawn grunt 3}\n");
    // A root value goes after the node before it, which is then bracketed.
    CHECK(print("(a 1) b 3") == "(a 1) b 3\n");
    // Past the width, a key with values keeps its groups as rows of brackets.
    std::string wide = "(walls";
    for (int i = 0; i < 12; ++i) wide += " (" + std::to_string(i) + " -5 14 0.2)";
    wide += ")";
    const std::string rows = to_dta(*brackets(wide), o);
    CHECK(rows.find("walls\n   (0 -5 14 0.2) (1 -5 14 0.2)") != std::string::npos);
    CHECK(to_string(Node(ok(lines(rows)))) == to_string(Node(brackets(wide))));
    // Today's form, where the layout reads it: a value that begins the root follows #lines.
    const std::string plain = to_dta(*brackets("5 (a 1)"));
    if (BAKER_INDENT_LINES == 1) CHECK(plain == "#lines 5\n(a 1)\n");
    CHECK(to_string(Node(ok(read_string(plain, "t.dta")))) == "(5 (a 1))");
    // The house style's defaults.
    CHECK(WriteOptions{}.indent == 3);
    CHECK(WriteOptions{}.width == 100);
}

#if !BAKER_SHIPPING
TEST_CASE("lines: write-back replaces one token; an inserted entry takes its parent's form") {
    TempDir dir;
    const std::string text =
        "; The test game's data.\nplayer\n   speed 3.5   ; how fast\n   lives 3\n"
        "(box\n   (height 3))\n";
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", "#lines\n" + text)));
    CHECK(cfg.source("player.speed")->line == 4);

    REQUIRE(cfg.set("player.speed", Node(4.5)));
    REQUIRE(cfg.write_back("player.speed"));
    CHECK(dir.read("default.dta") ==
          "#lines\n; The test game's data.\nplayer\n   speed 4.5   ; how fast\n   lives 3\n"
          "(box\n   (height 3))\n");

    // Into a line: a deeper line after the last child's trailing comment.
    dir.write("default.dta",
              "#lines\nplayer\n   speed 3.5   ; how fast\n   lives 3   ; per run\n(box\n"
              "   (height 3))\n");
    REQUIRE(cfg.reload());
    REQUIRE(cfg.set("player.jump.height", Node(2)));
    REQUIRE(cfg.write_back_to("player.jump.height", "default"));
    CHECK(dir.read("default.dta") ==
          "#lines\nplayer\n   speed 3.5   ; how fast\n   lives 3   ; per run\n   jump\n"
          "      height 2\n(box\n   (height 3))\n");
    CHECK(cfg.get<int>("player.jump.height") == 2);
    // Into brackets: brackets, never a line that would read as the parent's values.
    REQUIRE(cfg.set("box.width", Node(4)));
    REQUIRE(cfg.write_back_to("box.width", "default"));
    CHECK(dir.read("default.dta").ends_with("(box\n   (height 3)\n   (width 4))\n"));
    // At the top level, after brackets: brackets; after a line: a line.
    REQUIRE(cfg.set("audio.channels", Node(2)));
    REQUIRE(cfg.write_back_to("audio.channels", "default"));
    CHECK(dir.read("default.dta").ends_with("(width 4))\n(audio (channels 2))\n"));
    CHECK(cfg.get<int>("audio.channels") == 2);
    dir.write("default.dta", "#lines\nplayer\n   speed 3.5\n");
    REQUIRE(cfg.reload());
    REQUIRE(cfg.set("audio.channels", Node(2)));
    REQUIRE(cfg.write_back_to("audio.channels", "default"));
    CHECK(dir.read("default.dta") == "#lines\nplayer\n   speed 3.5\naudio\n   channels 2\n");
    CHECK(cfg.get<int>("audio.channels") == 2);
    CHECK(cfg.unsaved().empty());

    // Into a line that holds values: a bracket group, not a line its values would refuse.
    dir.write("default.dta", "#lines\nspeed 4.5\n   (range 0 10)\n");
    REQUIRE(cfg.reload());
    REQUIRE(cfg.set("speed.desc", Node(Symbol("fast"))));
    REQUIRE(cfg.write_back_to("speed.desc", "default"));
    CHECK(dir.read("default.dta") == "#lines\nspeed 4.5\n   (range 0 10)\n   (desc fast)\n");
}
#endif

TEST_CASE("lines: read_structure reports a line as a bracket of the line kind") {
    const std::string_view text =
        "#lines\nplayer\n   speed 3.5 (range 0 10)\n   lives 3\n(box (h 1))\n";
    const TextStructure s = read_structure(text, Symbol("t.dta"));
    CHECK(s.errors.empty());
    REQUIRE(s.brackets.size() == 6);
    CHECK(s.brackets[0].line_kind);
    CHECK(s.brackets[0].open == 0);
    CHECK(s.brackets[0].closed);
    CHECK(text.substr(s.brackets[0].begin, s.brackets[0].end - s.brackets[0].begin) ==
          "player\n   speed 3.5 (range 0 10)\n   lives 3");
    CHECK(s.brackets[1].parent == 0);
    CHECK(s.brackets[1].line_kind);
    CHECK(s.brackets[2].open == '(');
    CHECK(s.brackets[2].parent == 1);
    CHECK(s.brackets[4].open == '(');
    CHECK_FALSE(s.brackets[4].line_kind);
    const TextStructure bad = read_structure("#lines\nplayer\n   speed 3\n lives 1\n\tx 1\n");
    REQUIRE(bad.errors.size() == 2);
    CHECK(bad.errors[0].message() == "indent 1 matches no open line");
    CHECK(bad.errors[1].message() == "a tab in a line's indentation: indent with spaces");
}

#endif
