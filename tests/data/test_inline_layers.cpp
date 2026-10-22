// Layer blocks inside a file, (#layer HEAD form*): the parser lifts them out into the
// session, and a Config pushes them as sub-layers of their file
// (dev/syntax-inline-layers-design.md). Built only with BAKER_INLINE_LAYERS.
#include <doctest.h>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include "temp_dir.hpp"

#include <string>
#include <vector>
#include "ok.hpp"

#if BAKER_INLINE_LAYERS

using namespace baker;
using baker::test::ok;
using baker::test::TempDir;

namespace {

struct Read {
    LoadSession session;
    std::string tree;                 // the file's own tree
    std::vector<std::string> blocks;  // "name holds disabled line: tree"
    std::string joined;               // the same, " | " between
};

Read read(std::string_view text, std::vector<const char*> defines = {}) {
    Read r;
    for (const char* d : defines) r.session.define(d);
    auto root = read_string(text, "t.dta", r.session);
    REQUIRE_MESSAGE(root.has_value(), (root ? std::string() : to_string(root.error())));
    r.tree = to_string(**root);
    for (const Block& b : r.session.blocks())
        r.blocks.push_back(std::string(b.name.str()) + (b.holds ? " on" : " off") +
                           (b.disabled ? " disabled" : "") + " " + std::to_string(b.line) + ": " +
                           to_string(*b.tree));
    for (const std::string& b : r.blocks) r.joined += (r.joined.empty() ? "" : " | ") + b;
    return r;
}

Error error(std::string_view text) {
    LoadSession session;
    auto r = read_string(text, "t.dta", session);
    REQUIRE(!r.has_value());
    return r.error();
}

}  // namespace

TEST_CASE("a block's forms leave the tree and reach the session") {
    const Read r = read("(renderer (shadows 1))\n(#layer handheld (renderer (shadows 0)))\n");
    CHECK(r.tree == "((renderer (shadows 1)))");
    CHECK(r.blocks == std::vector<std::string>{"handheld on 2: ((renderer (shadows 0)))"});
}

TEST_CASE("a block's head: a name, (when COND), (disabled)") {
    CHECK(read("(#layer (hh (when HANDHELD)) (a 1))").blocks ==
          std::vector<std::string>{"hh off 1: ((a 1))"});
    CHECK(read("(#layer (hh (when HANDHELD)) (a 1))", {"HANDHELD"}).blocks ==
          std::vector<std::string>{"hh on 1: ((a 1))"});
    CHECK(read("(#layer (slow (disabled)) (a 1))").blocks ==
          std::vector<std::string>{"slow on disabled 1: ((a 1))"});
    CHECK(read("(#layer (x (disabled) (when A)) (a 1))", {"A"}).blocks ==
          std::vector<std::string>{"x on disabled 1: ((a 1))"});
    // A condition: a define name, (not C), (and C...), (or C...).
    CHECK(read("(#layer (x (when (not DEMO))) (a 1))").blocks[0] == "x on 1: ((a 1))");
    CHECK(read("(#layer (x (when (not DEMO))) (a 1))", {"DEMO"}).blocks[0] == "x off 1: ((a 1))");
    CHECK(read("(#layer (x (when (and A B))) (a 1))", {"A"}).blocks[0] == "x off 1: ((a 1))");
    CHECK(read("(#layer (x (when (and A B))) (a 1))", {"A", "B"}).blocks[0] == "x on 1: ((a 1))");
    CHECK(read("(#layer (x (when (or A B))) (a 1))", {"B"}).blocks[0] == "x on 1: ((a 1))");
    CHECK(read("(#layer (x (when (or A (not B)))) (a 1))", {"B"}).blocks[0] == "x off 1: ((a 1))");
    // A block with no forms is an empty layer.
    CHECK(read("(#layer empty)").blocks[0] == "empty on 1: ()");
    // The head's names are never macro-expanded, as #ifdef's is not.
    CHECK(
        read("[hh (zzz)]\n[HANDHELD 1]\n#define ON\n(#layer (hh (when HANDHELD)) (a 1))").blocks ==
        std::vector<std::string>{"hh on 4: ((a 1))"});
    CHECK(read("[hh (zzz)]\n#define ON\n(#layer hh (a hh))").blocks ==
          std::vector<std::string>{"hh on 3: ((a (zzz)))"});
}

TEST_CASE("a block inside tagged arrays is wrapped in their tags") {
    const Read r = read("(renderer (fog 1) (#layer (hh (when HANDHELD)) (shadows 0) (lod 2)))");
    CHECK(r.tree == "((renderer (fog 1)))");
    CHECK(r.joined == "hh off 1: ((renderer (shadows 0) (lod 2)))");
    const Read deep = read("(a (b (c 1) (#layer x (d 2))) (e 3))");
    CHECK(deep.tree == "((a (b (c 1)) (e 3)))");
    CHECK(deep.joined == "x on 1: ((a (b (d 2))))");
}

TEST_CASE("blocks keep #remove, and refuse every other directive") {
    const Read r = read("(enemies (boss 1))\n(#layer no_boss (enemies (boss #remove)))");
    CHECK(r.blocks == std::vector<std::string>{"no_boss on 2: ((enemies (boss #remove)))"});
    Error e = error("(#layer x #include other.dta)");
    CHECK(e.message() == "a layer block holds data: no #include inside (#layer ...)");
    CHECK(e.line() == 1);
    CHECK(e.column() == 11);
    CHECK(error("(#layer x #define Y)").message() ==
          "a layer block holds data: no #define inside (#layer ...)");
    CHECK(error("(#layer x #ifdef Y (a 1) #endif)").message() ==
          "a layer block holds data: no #ifdef inside (#layer ...)");
    CHECK(error("(#layer x #autorun {f})").message() ==
          "a layer block holds data: no #autorun inside (#layer ...)");
    CHECK(error("(#layer x (#layer y (a 1)))").message() ==
          "a layer block holds data: no #layer inside (#layer ...)");
}

TEST_CASE("a block's errors are located") {
    Error e = error("(a 1)\n(b #layer x)");
    CHECK(e.message() == "#layer is written first inside '(': (#layer name forms...)");
    CHECK(e.line() == 2);
    CHECK(e.column() == 4);
    CHECK(error("#layer x").message() ==
          "#layer is written first inside '(': (#layer name forms...)");
    CHECK(error("{#layer x}").message() ==
          "#layer is written first inside '(': (#layer name forms...)");
    CHECK(error("(#layer)").message() ==
          "#layer needs a name: (#layer name ...) or (#layer (name (when COND) (disabled)) ...)");
    CHECK(error("(#layer 3 (a 1))").message() ==
          "#layer needs a name: (#layer name ...) or (#layer (name (when COND) (disabled)) ...)");
    CHECK(error("(#layer ((when A)) (a 1))").message() ==
          "#layer needs a name: (#layer name ...) or (#layer (name (when COND) (disabled)) ...)");
    const char* const options =
        "block 'x': its options are (when COND) and (disabled), where COND is a define name, "
        "(not C), (and C...) or (or C...)";
    CHECK(error("(#layer (x (group arms)) (a 1))").message() == options);
    CHECK(error("(#layer (x (from \"2026\")) (a 1))").message() == options);
    CHECK(error("(#layer (x (dev_only)) (a 1))").message() == options);
    CHECK(error("(#layer (x (when)) (a 1))").message() == options);
    CHECK(error("(#layer (x (when A B)) (a 1))").message() == options);
    CHECK(error("(#layer (x (when 3)) (a 1))").message() == options);
    CHECK(error("(#layer (x (when (xor A B))) (a 1))").message() == options);
    CHECK(error("(#layer (x (when (not))) (a 1))").message() == options);
    CHECK(error("(#layer (x (disabled 1)) (a 1))").message() == options);
    // Inside an untagged array, a command or a macro definition.
    const char* const place = "a layer block sits at a file's top level or inside tagged arrays";
    CHECK(error("(1 2 (#layer x (a 1)))").message() == place);
    CHECK(error("((#layer x (a 1)))").message() == place);
    CHECK(error("(a {f (#layer x (a 1))})").message() == place);
    CHECK(error("[M (#layer x (a 1))]").message() == place);
    CHECK(error("#define M ((#layer x (a 1)))").message() == place);
    // Twice in one file.
    Error twice = error("(#layer x (a 1))\n(b 2)\n(#layer x (a 2))");
    CHECK(twice.message() == "block 'x' is declared twice in this file (first on line 1)");
    CHECK(twice.code() == ErrorCode::Refused);
    CHECK(twice.line() == 3);
    // Ledger's (when {...}) milestone field is data, not a block head.
    CHECK(read("(milestones (first (when {>= 1 1}) (target 60)))").blocks.empty());
    // The closing bracket.
    CHECK(error("(#layer x (a 1)]").message() == "'(' from line 1 is closed by ']'");
    CHECK(error("(#layer x (a 1)").message() == "'(' is never closed");
}

TEST_CASE("blocks inside an inactive #ifdef branch are skipped, and clear_blocks clears") {
    Read r = read("#ifdef NOPE\n(#layer x (a 1))\n#endif\n(b 2)");
    CHECK(r.tree == "((b 2))");
    CHECK(r.blocks.empty());
    r = read("#ifdef YES\n(#layer x (a 1))\n#endif\n(b 2)", {"YES"});
    CHECK(r.blocks.size() == 1);
    r.session.clear_blocks();
    CHECK(r.session.blocks().empty());
    // A copy of a session carries its blocks, as its autorun.
    Read again = read("(#layer x (a 1))");
    LoadSession copy = again.session;
    CHECK(copy.blocks().size() == 1);
}

#endif
