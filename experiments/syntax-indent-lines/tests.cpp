// indent-lines: the corpus round trip, a case for every form, and every error the reader
// reports (dev/syntax-indent-lines-design.md).
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
    auto r = read_string(text, "t.dta");
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
    // The printer cannot spell a root's bare scalar: this tree does not survive its round trip.
    const auto root = brackets("(a 1) b 3");
    const std::string printed = syntax::print(*root);
    CHECK(printed == "a 1\nb\n3\n");
    CHECK(!first_difference(*root, *lines(printed)).empty());
}

TEST_CASE("directives: a conditional at the margin, a define and a macro in a line block") {
    // #ifdef/#else/#endif around whole blocks, as ui_menu.dta writes them.
    same(
        "#ifdef NOT_DEFINED_HERE\nvocals_panel\n   lanes 3\n#else\nvocals_panel\n   lanes "
        "0\n#endif\n",
        "(vocals_panel (lanes 0))");
    // A directive's indent closes lines like a token's, and it opens none.
    same("player\n   speed 3\n   #ifndef NOT_DEFINED_HERE\n   lives 1\n   #endif\n",
         "(player (speed 3) (lives 1))");
    // #define with a body inside a block, then the macro used as a value and first on a line:
    // a macro name stands for its nodes, which splice in as today.
    same("player\n   #define kSpeed (3)\n   speed kSpeed\n", "(player (speed 3))");
    same("[kDefaults (a 1) (b 2)]\nplayer\n   kDefaults\n   c 3\n", "(player (a 1) (b 2) (c 3))");
    // #lines is accepted (the translator's layout is always on).
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
    CHECK(syntax::print(
              *brackets("(player (speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")))")) ==
          "player\n   speed 4.5 (range 0 10) (desc \"Run speed, in m/s\")\n");
    // A form with values, or without a tagged child, stays on one line.
    CHECK(syntax::print(*brackets("(items a b c) (waves {spawn grunt 3})")) ==
          "items a b c\nwaves {spawn grunt 3}\n");
    // Nested arrays stay on one line while they fit; untagged children are rows.
    CHECK(
        syntax::print(*brackets("(h (cottage (size 14 10) (walls (0 -5 14 0.2) (0 5 14 0.2))))")) ==
        "h\n   cottage (size 14 10) (walls (0 -5 14 0.2) (0 5 14 0.2))\n");
    // Past the width: a key with values keeps its groups in brackets on deeper rows; a key
    // without values gets its tagged children as lines.
    std::string wide = "(walls";
    for (int i = 0; i < 12; ++i) wide += " (" + std::to_string(i) + " -5 14 0.2)";
    wide += ")";
    const std::string rows = syntax::print(*brackets(wide));
    CHECK(rows.rfind("walls\n   (0 -5 14 0.2) (1 -5 14 0.2)", 0) == 0);
    CHECK(first_difference(*brackets(wide), *lines(rows)).empty());
    // A head wider than the width falls back to the bracketed pretty form.
    const std::string text = "(desc \"" + std::string(120, 'x') + "\")";
    const std::string fallback = syntax::print(*brackets(text));
    CHECK(fallback.rfind("(desc\n   \"xxx", 0) == 0);
    CHECK(first_difference(*brackets(text), *lines(fallback)).empty());
    // An untagged or non-array top-level node is bracketed as today.
    CHECK(syntax::print(*brackets("(1 2) {cmd} \"s\"")) == "(1 2)\n{cmd}\n\"s\"\n");
    CHECK(syntax::print(*brackets("")).empty());
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
