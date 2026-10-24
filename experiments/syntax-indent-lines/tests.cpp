// indent-lines: the corpus round trip, a case for every form, and every error the reader
// reports (dev/syntax-indent-lines-design.md). syntax::read is the core's reader in a build
// that reads lines (BAKER_INDENT_LINES) and the translator otherwise; the cases hold for both.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <doctest.h>

#include <string>

using namespace baker;
using namespace baker::experiments;

namespace {

// A read, or an empty array after a failed check (no exceptions: the case goes on).
Ref<Array> lines(std::string_view text) {
    auto r = syntax::read(text, "t.dta");
    CHECK_MESSAGE(r, (r ? "" : to_string(r.error())));
    return r ? *r : Array::make(0);
}
Ref<Array> brackets(std::string_view text) {
    LoadSession session;
#if BAKER_INDENT_LINES
    session.lines = false;  // today's reading, in a build that reads lines too
#endif
    auto r = read_string(text, "t.dta", session);
    CHECK_MESSAGE(r, (r ? "" : to_string(r.error())));
    return r ? *r : Array::make(0);
}
// The lines text reads to the same tree as the bracketed text.
void same(std::string_view lines_text, std::string_view bracketed) {
    CAPTURE(lines_text);
    const std::string diff = first_difference(*lines(lines_text), *brackets(bracketed));
    CHECK(diff.empty());
    if (!diff.empty()) MESSAGE(diff);
}
// The printer's text, without the directive an opt-in build writes first.
std::string printed(const Array& root) {
    std::string s = syntax::print(root);
    if (BAKER_INDENT_LINES == 2 && s.rfind("#lines\n", 0) == 0) s.erase(0, 7);
    return s;
}
Error error_of(std::string_view text) {
    auto r = syntax::read(text, "t.dta");
    CHECK_MESSAGE(!r, "read as " << (r ? to_dta(**r) : std::string()));
    return r ? Error() : r.error();
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

TEST_CASE("the brief's snippets: a line is an array, its deeper lines its children") {
    // examples/quickstart/default.dta
    same("player\n   speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")\n",
         "(player (speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")))");
    // tests/host/stack/data/default.dta, with its trailing comment
    same(
        "shapes\n"
        "   player\n"
        "      speed float (range 0 10)\n"
        "      lives int\n"
        "player\n"
        "   speed 3.5   ; how fast\n"
        "   lives 3\n"
        "waves {spawn grunt 3}\n",
        "(shapes (player (speed float (range 0 10)) (lives int)))\n"
        "(player (speed 3.5) (lives 3))\n"
        "(waves {spawn grunt 3})\n");
    // stacks/hearth/data/default.dta:45-55: tuple rows keep their parentheses under their key
    same(
        "households\n"
        "   cottage\n"
        "      desc \"Two rooms and a couple\"\n"
        "      size 14 10\n"
        "      walls\n"
        "         (0 -5 14 0.2) (0 5 14 0.2)\n"
        "         (1 -3 0.2 4)\n"
        "      residents\n"
        "         ada (weights (social 1.4)) (color 0.95 0.55 0.4)\n",
        "(households (cottage (desc \"Two rooms and a couple\") (size 14 10)\n"
        "   (walls (0 -5 14 0.2) (0 5 14 0.2) (1 -3 0.2 4))\n"
        "   (residents (ada (weights (social 1.4)) (color 0.95 0.55 0.4)))))");
}

TEST_CASE("each form of a line") {
    same("landmark\n", "(landmark)");        // an atom alone
    same("pos 34 0 22\n", "(pos 34 0 22)");  // a tag and values
    same("'bacon and eggs' (price 12.75)\n", "('bacon and eggs' (price 12.75))");  // quoted tag
    same("speed $base\n", "(speed $base)");                      // a variable value
    same("waves {spawn grunt 3}\n", "(waves {spawn grunt 3})");  // a command value
    same("hud [focus_ring color]\n", "(hud [focus_ring color])");
    same("key #remove\n", "(key #remove)");  // the marker as a line's one value
    // A bracket-first line adds no array: its groups are the enclosing line's nodes.
    same("walls\n   (0 -5 14 0.2) (0 5 14 0.2)\n   (1 -3 0.2 4)\n",
         "(walls (0 -5 14 0.2) (0 5 14 0.2) (1 -3 0.2 4))");
    // A bracket group spanning lines inside a line block: indentation means nothing in it.
    same("hud\n   (menu\n  (x 1)\n        (y 2))\n   z 3\n", "(hud (menu (x 1) (y 2)) (z 3))");
    // A multi-line string does not start a line; the line after it does.
    same("a\n   desc \"one\ntwo\" 3\n   b 4\n", "(a (desc \"one\\ntwo\" 3) (b 4))");
    // Comments: a trailing one, a whole line, and a block comment between lines.
    same("a ; x\n   ; y\n   b 1 /* z */\n   /* w */\n   c 2\n", "(a (b 1) (c 2))");
    // Siblings at the margin and a dedent by two levels at once.
    same("a\n   b\n      c 1\nd 2\n", "(a (b (c 1))) (d 2)");
    // A number first on a line is an array too, as the grammar says, though nothing writes it.
    same("1 2 3\n", "(1 2 3)");
}

TEST_CASE("a bracketed file reads as today") {
    same("(a 1) b 3\n", "(a 1) b 3");  // b and 3 are not first on their line: root values
    same("(player\n   (speed 3.5)\n   (lives 3))\n", "(player (speed 3.5) (lives 3))");
    same("(a 1)\n   (b 2)\n(c 3)\n", "(a 1) (b 2) (c 3)");  // indentation means nothing to brackets
    same("(a\n\t(b 1))\n", "(a (b 1))");                    // a tab inside brackets is whitespace
}

TEST_CASE("the gate: a bare atom first on its line at the top level becomes an array") {
    same("b\n", "(b)");
    same("(a 1)\nb 3\n", "(a 1) (b 3)");
    CHECK(syntax::top_level_lines("(a 1)\nb 3\n") == 1);
    CHECK(syntax::top_level_lines("(a 1) b 3\n") == 0);
    CHECK(syntax::top_level_lines("(a\n   b 3)\n") == 0);
    // A root's value is printed after the node before it, which is then bracketed; one
    // that begins the root follows #lines, a directive that opens no line.
    const auto root = brackets("(a 1) b 3");
    CHECK(printed(*root) == "(a 1) b 3\n");
    CHECK(first_difference(*root, *lines(syntax::print(*root))).empty());
    const auto first = brackets("5 (a 1)");
    CHECK(syntax::print(*first) == "#lines 5\na 1\n");
    CHECK(first_difference(*first, *lines(syntax::print(*first))).empty());
    // A top-level line begins at the margin: indented, it is an error, not a silent array.
    CHECK(error_of("(a 1)\n   b 3\n").message() == "indent 3 matches no open line");
}

TEST_CASE("directives: a conditional at the margin, a define and a macro in a line block") {
    // #ifdef/#else/#endif around whole blocks, as ui_menu.dta writes them.
    same(
        "#ifdef NOT_DEFINED_HERE\nvocals_panel\n   lanes 3\n#else\nvocals_panel\n   lanes "
        "0\n#endif\n",
        "(vocals_panel (lanes 0))");
    // A conditional takes no part in the layout: at the margin inside a block, too.
    same("player\n   speed 3\n#ifndef NOT_DEFINED_HERE\n   lives 1\n#endif\n   jump 2\n",
         "(player (speed 3) (lives 1) (jump 2))");
    // Any other directive's indent closes lines like a token's, and it opens none: after
    // one at the margin, an indented line has no line to belong to.
    CHECK(error_of("player\n   speed 3\n#define FAST\n   lives 3\n").message() ==
          "indent 3 matches no open line");
    // #define with a body inside a block, then the macro used as a value and first on a line:
    // a macro name stands for its nodes, which splice in as today.
    same("player\n   #define kSpeed (3)\n   speed kSpeed\n", "(player (speed 3))");
    same("[kDefaults (a 1) (b 2)]\nplayer\n   kDefaults\n   c 3\n", "(player (a 1) (b 2) (c 3))");
    // #lines is accepted (the layout is on from the start here).
    same("#lines\nplayer\n   speed 3\n", "(player (speed 3))");
    // A pack declaration as a line reaches the pack machinery: no levels are set here.
    const Error e = error_of("pack hearth_core (level game)\n");
    CHECK(e.code() == ErrorCode::Directive);
    CHECK(e.message().find("pack levels") != std::string_view::npos);
}

TEST_CASE("errors are located: the dedent that lands between levels") {
    const Error e = error_of("player\n   speed 3\n lives 1\n");
    CHECK(e.code() == ErrorCode::Syntax);
    CHECK(e.message() == "indent 1 matches no open line");
    CHECK(e.file().str() == "t.dta");
    CHECK(e.line() == 3);
    CHECK(e.column() == 2);
    // A dedent onto an outer line's indent is a sibling, well-formed: the limit the note states.
    same("a\n   b\n      c 1\n   d 2\n", "(a (b (c 1)) (d 2))");
}

TEST_CASE("errors are located: a tab in a line's indentation") {
    const Error e = error_of("player\n\tspeed 3\n");
    CHECK(e.code() == ErrorCode::Syntax);
    CHECK(e.message() == "a tab in a line's indentation: indent with spaces");
    CHECK(e.line() == 2);
    CHECK(e.column() == 2);
}

TEST_CASE("errors are located: a value list continued on a deeper line") {
    // The trap of four sim files: (objectives wayfarer.a / wayfarer.b wayfarer.c)).
    const Error e = error_of("objectives wayfarer.a\n   wayfarer.b wayfarer.c\n");
    CHECK(e.code() == ErrorCode::Syntax);
    CHECK(e.message() ==
          "a value list continues on the same line or in brackets; a deeper line is a child");
    CHECK(e.line() == 2);
    CHECK(e.column() == 4);
    // A deeper bracket-first line under a line with values is allowed: groups, not a child.
    same("speed 4.5\n   (range 0 10) (desc \"x\")\n", "(speed 4.5 (range 0 10) (desc \"x\"))");
}

TEST_CASE("errors from the parser keep their column on a shifted line") {
    const Error e = error_of("speed $\n");  // the bracket inserted at column 1 is not counted
    CHECK(e.message() == "'$' without a variable name");
    CHECK(e.line() == 1);
    CHECK(e.column() == 7);
    const Error f = error_of("a\n   speed $\n");  // indented: the bracket took a space's place
    CHECK(f.line() == 2);
    CHECK(f.column() == 10);
    const Error g = error_of("a\n   b 1)\n");
    CHECK(g.message() == "unexpected ')'");
    CHECK(g.line() == 2);
}

TEST_CASE("the printer: sections, lines, rows and the bracket fallback") {
    // A top-level form with tagged children is a section even when it would fit on a line.
    CHECK(printed(*brackets("(player (speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")))")) ==
          "player\n   speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")\n");
    // A form with values, or without a tagged child, stays on one line.
    CHECK(printed(*brackets("(items a b c) (waves {spawn grunt 3})")) ==
          "items a b c\nwaves {spawn grunt 3}\n");
    // Nested arrays stay on one line while they fit; untagged children are rows.
    CHECK(printed(*brackets("(h (cottage (size 14 10) (walls (0 -5 14 0.2) (0 5 14 0.2))))")) ==
          "h\n   cottage (size 14 10) (walls (0 -5 14 0.2) (0 5 14 0.2))\n");
    // Past the width: a key with values keeps its groups in brackets on deeper rows; a key
    // without values gets its tagged children as lines.
    std::string wide = "(walls";
    for (int i = 0; i < 12; ++i) wide += " (" + std::to_string(i) + " -5 14 0.2)";
    wide += ")";
    const std::string rows = printed(*brackets(wide));
    CHECK(rows.rfind("walls\n   (0 -5 14 0.2) (1 -5 14 0.2)", 0) == 0);
    CHECK(first_difference(*brackets(wide), *lines(rows)).empty());
    // A head wider than the width falls back to the bracketed pretty form.
    const std::string text = "(desc \"" + std::string(120, 'x') + "\")";
    const std::string fallback = printed(*brackets(text));
    CHECK(fallback.rfind("(desc\n   \"xxx", 0) == 0);
    CHECK(first_difference(*brackets(text), *lines(fallback)).empty());
    // An untagged top-level array or command is bracketed as today; a value follows it.
    CHECK(printed(*brackets("(1 2) {cmd} \"s\"")) == "(1 2)\n{cmd} \"s\"\n");
    CHECK(printed(*brackets("")).empty());
}

TEST_CASE("a converted sample reads back to the same tree") {
    auto original = syntax::read(
        "(menu ('bacon and eggs' (price 12.75) (calories 120)))\n"
        "(rules {func scaled ($base) {* $base $difficulty}})\n",
        "sample.dta");
    REQUIRE(original);
    const std::string converted = syntax::print(**original);
    auto back = syntax::read(converted, "sample.new");
    REQUIRE(back);
    CHECK(first_difference(**original, **back).empty());
}

TEST_CASE("the translator and the printer agree with the core's, where the build has it") {
    if (BAKER_INDENT_LINES == 0) return;  // the translator is the reader: nothing to compare
    size_t files = 0;
    for (const std::string& rel : corpus_files()) {
        auto root = read_dta_file(std::string(repository_root()) + "/" + rel);
        if (!root) continue;
        ++files;
        INFO(rel);
        const std::string core = printed(**root);
        CHECK(syntax::translator_print(**root) == core);
        auto by_core = syntax::read(core, "t.dta");
        auto by_translator = syntax::translate(core, "t.dta");
        REQUIRE(by_core);
        REQUIRE(by_translator);
        CHECK(first_difference(**by_core, **by_translator).empty());
    }
    CHECK(files > 50);
}

TEST_CASE("a conditional's branch: a line opened in it closes in it (the translator)") {
    // The translator does not evaluate conditionals, so it closes a line in the branch it
    // opened in; the core keeps it open past the branch. They agree where a line does not
    // outlive its branch, as here.
    same("a\n   b 1\n#ifdef X\n   c 2\n#endif\nd 3\n", "(a (b 1)) (d 3)");
    same("#ifndef X\nlist\n   (1 2)\n#else\nlist\n   (3 4)\n#endif\nnext 1\n",
         "(list (1 2)) (next 1)");
}

TEST_CASE("the migration tool keeps comments and directives, and every tree") {
    const syntax::Conversion c = syntax::to_lines_text(
        "; The test game's data.\n(shapes\n   (player\n      (speed float (range 0 10))\n"
        "      (lives int)))\n(player\n   (speed 3.5)   ; how fast\n   (lives 3))\n"
        "(waves {spawn grunt 3})\n");
    CHECK(c.text ==
          "; The test game's data.\nshapes\n   player\n      speed float (range 0 10)\n"
          "      lives int\nplayer\n   speed 3.5   ; how fast\n   lives 3\n"
          "waves {spawn grunt 3}\n");
    CHECK(c.arrays == 9);
    CHECK(c.lines == 8);
    // Kept: a child of a key holding a value, a continued value list, a shared line.
    const std::string kept =
        "(objective a\n   (desc \"x\"))\n(objectives a\n   b c)\n(x 1) (y 2)\n(z\n   (w 1)\n)\n";
    const syntax::Conversion k = syntax::to_lines_text(kept);
    CHECK(k.text ==
          "objective a\n   (desc \"x\")\n(objectives a\n   b c)\n(x 1) (y 2)\nz\n   w 1\n");
    CHECK(k.under_values == 1);
    CHECK(k.continued == 1);
    CHECK(k.shares_line == 1);
    CHECK(first_difference(*brackets(kept), *lines(k.text)).empty());
    // A row wrapped under the node after a tag stays a row; children at a body indent are
    // lines, even after groups on their parent's line.
    const std::string rows =
        "(objects\n   (fridge (size 1) (color 2)\n           (minutes 15)))\n"
        "(shapes\n   (fighter (at fighters) (keyed)\n      (health int)))\n";
    const syntax::Conversion r = syntax::to_lines_text(rows);
    CHECK(r.text ==
          "objects\n   fridge (size 1) (color 2)\n          (minutes 15)\nshapes\n"
          "   fighter (at fighters) (keyed)\n      health int\n");
    CHECK(r.beside_tag == 1);
    CHECK(first_difference(*brackets(rows), *lines(r.text)).empty());

    size_t files = 0;
    for (const std::string& rel : corpus_files()) {
        const std::string path = std::string(repository_root()) + "/" + rel;
        auto original = read_dta_file(path);
        if (!original) continue;
        ++files;
        INFO(rel);
        const syntax::Conversion converted = syntax::to_lines_text(*read_text_file(path));
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        auto back = syntax::read_with(converted.text, path, session);
        REQUIRE(back);
        CHECK(first_difference(**original, **back).empty());
    }
    CHECK(files > 50);
}
