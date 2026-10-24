// Layer blocks inside a file, (#layer HEAD form*): the parser lifts them out into the
// session, and a Config pushes them as sub-layers of their file. A prototype, built only
// with BAKER_INLINE_LAYERS.
#include <doctest.h>
#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include "temp_dir.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include "ok.hpp"

#if BAKER_INLINE_LAYERS

using namespace baker;
using baker::test::ok;
using baker::test::TempDir;
namespace fs = std::filesystem;

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
    CHECK(twice.message() == "block 'x' is declared twice (first at t.dta:1)");
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

// --- A Config's blocks ---

#if BAKER_INLINE_LAYERS

namespace {

// examples/config/data, as two files and as one with a block.
const char* const kDefault =
    "; Shared settings, checked in. Each subsystem reads only its own group.\n"
    "(renderer\n"
    "   (shadows 1 (desc \"Cast shadows\"))\n"
    "   (clear_color 0.1 0.1 0.2 (desc \"Background\")))\n"
    "(audio\n"
    "   (volume 0.8 (range 0 1) (step 0.05) (desc \"Master volume\"))\n"
    "   (channels 32))\n";
const char* const kHandheldFile =
    "; The handheld platform: only what differs from default.dta.\n"
    "(renderer\n   (shadows 0))\n";
const char* const kHandheldBlock =
    "; The handheld platform: only what differs, as a block of this file.\n"
    "(#layer (handheld (when HANDHELD))\n"
    "   (renderer (shadows 0)))\n";

std::string explain_of(const Config& cfg, const char* path) {
    static const char* const kStanding[] = {"wins", "shadowed", "removes", "disabled"};
    std::string out;
    for (const ExplainEntry& e : ok(cfg.explain(path)))
        out += std::string(e.layer.str()) + " " + kStanding[int(e.standing)] + " " +
               to_string(e.value) + "; ";
    return out;
}

std::string effect_of(const Config& cfg, const char* layer) {
    std::string out;
    for (const LayerEffectEntry& e : ok(cfg.layer_effect(layer)))
        out += std::string(e.path.str()) + "=" + to_string(e.value) +
               (e.below ? " over " + to_string(*e.below) : std::string()) + "; ";
    return out;
}

}  // namespace

TEST_CASE("handheld as a block equals handheld as a file") {
    TempDir dir;
    Config files({.tweaks = nullptr});
    REQUIRE(files.push_layer("default", dir.write("files/default.dta", kDefault)));
    REQUIRE(files.push_layer("handheld", dir.write("files/handheld.dta", kHandheldFile)));
    Config one({.tweaks = nullptr});
    one.session().define("HANDHELD");
    REQUIRE(one.push_layer("default",
                           dir.write("one/default.dta", std::string(kDefault) + kHandheldBlock)));
    CHECK(strings(one.layers()) ==
          std::vector<std::string>{"default", "default:handheld", "scratch"});
    CHECK(one.get<int>("renderer.shadows") == 0);
    CHECK(one.get<int>("renderer.shadows") == *files.get<int>("renderer.shadows"));
    CHECK(one.get<float>("audio.volume") == *files.get<float>("audio.volume"));
    CHECK(structurally_equal(*one.root(), *files.root()));
    // source: the block's name, and the block's own line in the one file.
    const Source s = ok(one.source("renderer.shadows"));
    CHECK(s.layer == Symbol("default:handheld"));
    CHECK(s.file.str().ends_with("one/default.dta"));
    CHECK(s.line == 10);
    const Source f = ok(files.source("renderer.shadows"));
    CHECK(f.layer == Symbol("handheld"));
    CHECK(f.line == 3);
    // explain and layer_effect agree, the layer's name apart.
    CHECK(explain_of(one, "renderer.shadows") == "default shadowed 1; default:handheld wins 0; ");
    CHECK(explain_of(files, "renderer.shadows") == "default shadowed 1; handheld wins 0; ");
    CHECK(effect_of(one, "default:handheld") == "renderer.shadows=0 over 1; ");
    CHECK(effect_of(one, "default:handheld") == effect_of(files, "handheld"));
    // Without the define the block is read, kept, and disabled.
    Config off({.tweaks = nullptr});
    REQUIRE(off.push_layer("default", dir.path("one/default.dta")));
    CHECK(off.get<int>("renderer.shadows") == 1);
    CHECK_FALSE(off.layer_enabled("default:handheld"));
    CHECK(explain_of(off, "renderer.shadows") == "default wins 1; default:handheld disabled 0; ");
    REQUIRE(off.set_layer_enabled("default:handheld", true));
    CHECK(off.get<int>("renderer.shadows") == 0);
    // layer_root and diff take a block's name.
    CHECK(off.layer_root("default:handheld") != nullptr);
    int shadows = 0;
    for (const DiffEntry& d : ok(off.diff("default", "default:handheld")))
        if (d.path == Symbol("renderer.shadows"))
            shadows += d.a && d.b && d.a->try_int() == 1 && d.b->try_int() == 0;
    CHECK(shadows == 1);
}

TEST_CASE("ui_menu's conditionals as blocks merge to what #ifdef gave, per define set") {
    const char* const ifdefs =
        "(main_menu (focus play_button))\n"
        "#ifdef HAS_VOCALS\n(vocals_panel (lanes 3) (pitch_arrow TRUE))\n#else\n"
        "(vocals_panel (lanes 0))\n#endif\n"
        "#ifndef DEMO_BUILD\n(store_button (text \"Store\"))\n#endif\n";
    const char* const blocks =
        "(main_menu (focus play_button))\n"
        "(vocals_panel (lanes 0))\n"
        "(#layer (vocals (when HAS_VOCALS)) (vocals_panel (lanes 3) (pitch_arrow TRUE)))\n"
        "(#layer (store (when (not DEMO_BUILD))) (store_button (text \"Store\")))\n";
    TempDir dir;
    const std::string a = dir.write("ifdefs.dta", ifdefs), b = dir.write("blocks.dta", blocks);
    for (int set = 0; set < 4; ++set) {
        Config x({.tweaks = nullptr}), y({.tweaks = nullptr});
        if (set & 1) {
            x.session().define("HAS_VOCALS");
            y.session().define("HAS_VOCALS");
        }
        if (set & 2) {
            x.session().define("DEMO_BUILD");
            y.session().define("DEMO_BUILD");
        }
        REQUIRE(x.push_layer("menu", a));
        REQUIRE(y.push_layer("menu", b));
        INFO("define set " << set << ": " << to_string(*x.root()) << " vs "
                           << to_string(*y.root()));
        CHECK(structurally_equal(*x.root(), *y.root()));
        CHECK(y.layer_enabled("menu:vocals") == bool(set & 1));
        CHECK(y.layer_enabled("menu:store") == !(set & 2));
    }
}

TEST_CASE("a disabled block is a layer to toggle, and snapshot and pop take the unit") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer(
        "game", dir.write("game.dta",
                          "(bosses (dragon (speed 1.0)))\n"
                          "(#layer boss_faster (bosses (dragon (speed 1.5))))\n"
                          "(#layer (boss_slower (disabled)) (bosses (dragon (speed 0.8))))\n")));
    CHECK(strings(cfg.layers()) ==
          std::vector<std::string>{"game", "game:boss_faster", "game:boss_slower", "scratch"});
    CHECK(cfg.get<float>("bosses.dragon.speed") == 1.5f);
    const ConfigSnapshot fast = cfg.snapshot();
    REQUIRE(cfg.set_layer_enabled("game:boss_faster", false));
    REQUIRE(cfg.set_layer_enabled("game:boss_slower", true));
    CHECK(cfg.get<float>("bosses.dragon.speed") == 0.8f);
    CHECK(ok(cfg.diff("game:boss_slower", "game:boss_faster")).size() == 1);
    REQUIRE(cfg.restore(fast));
    CHECK(cfg.get<float>("bosses.dragon.speed") == 1.5f);
    CHECK(cfg.layer_enabled("game:boss_faster"));
    // A block's name is taken: a layer with it is refused, as is removing the block alone.
    CHECK(cfg.push_layer("game:boss_faster", dir.write("x.dta", "(a 1)")).error().code() ==
          ErrorCode::Refused);
    Error e = cfg.remove_layer("game:boss_faster").error();
    CHECK(e.message() ==
          "'game:boss_faster' is a block of its file: disable it, or remove the file's layer");
    REQUIRE(cfg.push_layer("top", dir.write("top.dta", "(a 1)")));
    REQUIRE(cfg.pop_layer());
    REQUIRE(cfg.pop_layer());  // the file and its blocks
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"scratch"});
}

TEST_CASE("remove_layer removes a file's blocks, and placement treats them as one unit") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("base", dir.write("base.dta", "(songs (song_a (bpm 120)))\n")));
    REQUIRE(cfg.push_layer("tu2",
                           dir.write("tu2.dta",
                                     "(songs (song_c (bpm 141)))\n"
                                     "(#layer fix (songs (song_c (bpm 142))))\n"
                                     "#ifdef HARD\n(level hard)\n#else\n(level easy)\n#endif\n")));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"base", "tu2", "tu2:fix", "scratch"});
    CHECK(cfg.get<int>("songs.song_c.bpm") == 142);
    // Below a block is below its file; above a file is above its blocks (D144), and the
    // layers above a placed file are read again with its macros, blocks remade.
    REQUIRE(cfg.push_layer("pack1", dir.write("pack1.dta", "(songs (song_c (bpm 140)))\n"),
                           {.below = "tu2:fix"}));
    CHECK(strings(cfg.layers()) ==
          std::vector<std::string>{"base", "pack1", "tu2", "tu2:fix", "scratch"});
    CHECK(cfg.get<int>("songs.song_c.bpm") == 142);
    REQUIRE(cfg.push_layer("mode", dir.write("mode.dta", "#define HARD\n(#layer m (x 1))\n"),
                           {.below = "tu2"}));
    CHECK(strings(cfg.layers()) ==
          std::vector<std::string>{"base", "pack1", "mode", "mode:m", "tu2", "tu2:fix", "scratch"});
    CHECK(cfg.get<Symbol>("level") == Symbol("hard"));
    REQUIRE(cfg.push_layer("after", dir.write("after.dta", "(y 2)\n"), {.above = "mode"}));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"base", "pack1", "mode", "mode:m",
                                                            "after", "tu2", "tu2:fix", "scratch"});
    REQUIRE(cfg.remove_layer("mode"));
    CHECK(strings(cfg.layers()) ==
          std::vector<std::string>{"base", "pack1", "after", "tu2", "tu2:fix", "scratch"});
    CHECK(cfg.get<Symbol>("level") == Symbol("easy"));
    REQUIRE(cfg.remove_layer("tu2"));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"base", "pack1", "after", "scratch"});
    CHECK(cfg.get<int>("songs.song_c.bpm") == 140);
}

TEST_CASE("reload after editing a block keeps a host's toggle unless its head changed") {
    TempDir dir;
    const std::string path =
        dir.write("d.dta", "(a 1)\n(#layer (x (disabled)) (a 2))\n(#layer y (b 3))\n");
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", path));
    CHECK(cfg.get<int>("a") == 1);
    REQUIRE(cfg.set_layer_enabled("d:x", true));
    REQUIRE(cfg.set_layer_enabled("d:y", false));
    CHECK(cfg.get<int>("a") == 2);
    // A value edited in a block: the reload re-reads the file and its blocks; the toggles stay.
    dir.write("d.dta", "(a 1)\n(#layer (x (disabled)) (a 20))\n(#layer y (b 3))\n");
    CHECK(ok(cfg.reload()));
    CHECK(cfg.get<int>("a") == 20);
    CHECK(cfg.layer_enabled("d:x"));
    CHECK_FALSE(cfg.layer_enabled("d:y"));
    CHECK(cfg.last_reload()->files_reloaded == 1);
    // The head edited: what it says now wins over the toggle.
    dir.write("d.dta", "(a 1)\n(#layer x (a 20))\n(#layer (y (disabled)) (b 3))\n");
    CHECK(ok(cfg.reload()));
    CHECK(cfg.layer_enabled("d:x"));
    CHECK_FALSE(cfg.layer_enabled("d:y"));
    // A block added or removed by the edit.
    dir.write("d.dta", "(a 1)\n(#layer z (c 4))\n");
    CHECK(ok(cfg.reload()));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"d", "d:z", "scratch"});
    CHECK(cfg.get<int>("c") == 4);
    // A broken head on disk is a parse error, and the stack stands.
    dir.write("d.dta", "(a 1)\n(#layer (z (group arms)) (c 4))\n");
    CHECK_FALSE(cfg.reload());
    CHECK(cfg.get<int>("c") == 4);
}

TEST_CASE("write_back_to into a block, and promote out of it into its file") {
    TempDir dir;
    const std::string path = dir.write("d.dta",
                                       "(renderer\n   (shadows 1)\n   (fog 1))\n"
                                       "(#layer (hh (when HANDHELD))\n"
                                       "   (renderer (shadows 0)))\n");
    Config cfg({.tweaks = nullptr});
    cfg.session().define("HANDHELD");
    REQUIRE(cfg.push_layer("d", path));
    // The value the block holds: its token is replaced, inside the block.
    REQUIRE(cfg.set("renderer.shadows", Node(2)));
    REQUIRE(cfg.write_back("renderer.shadows"));
    CHECK(dir.read("d.dta") ==
          "(renderer\n   (shadows 1)\n   (fog 1))\n"
          "(#layer (hh (when HANDHELD))\n"
          "   (renderer (shadows 2)))\n");
    CHECK(cfg.source("renderer.shadows")->layer == Symbol("d:hh"));
    // A value the block lacks goes inside its extent, in the deepest ancestor it has.
    REQUIRE(cfg.set("renderer.fog", Node(0)));
    REQUIRE(cfg.write_back_to("renderer.fog", "d:hh"));
    CHECK(dir.read("d.dta") ==
          "(renderer\n   (shadows 1)\n   (fog 1))\n"
          "(#layer (hh (when HANDHELD))\n"
          "   (renderer (shadows 2) (fog 0)))\n");
    CHECK(cfg.get<int>("renderer.fog") == 0);
    CHECK(cfg.source("renderer.fog")->layer == Symbol("d:hh"));
    // A new top-level key into the block: after its last form, inside the brackets.
    REQUIRE(cfg.set("audio.volume", Node(0.5)));
    REQUIRE(cfg.write_back_to("audio.volume", "d:hh"));
    CHECK(dir.read("d.dta") ==
          "(renderer\n   (shadows 1)\n   (fog 1))\n"
          "(#layer (hh (when HANDHELD))\n"
          "   (renderer (shadows 2) (fog 0))\n   (audio (volume 0.5)))\n");
    CHECK(cfg.unsaved().empty());
    // Promote the block into its file: the file's own tokens change; the block stays.
    const WriteReport r = ok(cfg.promote("d:hh", "d"));
    CHECK(r.ok());
    CHECK(dir.read("d.dta") ==
          "(renderer\n   (shadows 2)\n   (fog 0))\n"
          "(#layer (hh (when HANDHELD))\n"
          "   (renderer (shadows 2) (fog 0))\n   (audio (volume 0.5)))\n"
          "(audio (volume 0.5))\n");
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"d", "d:hh", "scratch"});
    CHECK(cfg.get<int>("renderer.shadows") == 2);
    // A block inside a tagged array writes back through its wrapper.
    const std::string wrapped = dir.write("w.dta", "(renderer (fog 1) (#layer hh (shadows 0)))\n");
    Config two({.tweaks = nullptr});
    REQUIRE(two.push_layer("w", wrapped));
    REQUIRE(two.set("renderer.shadows", Node(3)));
    REQUIRE(two.write_back("renderer.shadows"));
    REQUIRE(two.set("renderer.lod", Node(2)));
    REQUIRE(two.write_back_to("renderer.lod", "w:hh"));
    CHECK(dir.read("w.dta") == "(renderer (fog 1) (#layer hh (shadows 3) (lod 2)))\n");
}

TEST_CASE("a text overlay's blocks are admitted under the policy, each a candidate") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer(
        "default", dir.write("default.dta", "(event (xp 1) (drops 5) (boss (speed 1.0)))\n")));
    const LayerPolicy live{.data_only = true, .scalars_only = true, .existing_only = true};
    const char* const overlay =
        "(event (xp 2))\n"
        "(#layer (weekend (when WEEKEND)) (event (drops 10)))\n"
        "(#layer (slow_boss (disabled)) (event (boss (speed 0.5))))\n";
    REQUIRE(cfg.push_layer_text("received/ops@1.dta", overlay, {.policy = &live}));
    CHECK(strings(cfg.layers()) ==
          std::vector<std::string>{"default", "received/ops@1.dta", "received/ops@1.dta:weekend",
                                   "received/ops@1.dta:slow_boss", "scratch"});
    CHECK(cfg.get<int>("event.xp") == 2);
    CHECK(cfg.get<int>("event.drops") == 5);
    REQUIRE(cfg.set_layer_enabled("received/ops@1.dta:weekend", true));
    CHECK(cfg.get<int>("event.drops") == 10);
    // Refused when one block holds a command under data_only; nothing changes.
    const char* const scripted =
        "(event (xp 3))\n(#layer (hack (when WEEKEND)) (event (drops {steal 99})))\n";
    const Error e = cfg.push_layer_text("received/ops@2.dta", scripted, {.policy = &live}).error();
    CHECK(e.code() == ErrorCode::Refused);
    CHECK(e.message() ==
          "'event.drops' holds {steal 99}; the layer may hold data only (and 1 more)");
    CHECK(cfg.get<int>("event.xp") == 2);
    // check_layer_text lists each block's effect and refusals under FILE:NAME.
    const LayerReport report = ok(cfg.check_layer_text("received/ops@2.dta", scripted, live));
    CHECK_FALSE(report.ok());
    CHECK(report.effect.size() == 2);
    CHECK(report.effect[1].source.layer == Symbol("received/ops@2.dta:hack"));
    CHECK(report.refusals.size() == 2);  // a command, and a value that is not a scalar
    // A received block cannot be written back: its text has no file.
    REQUIRE(cfg.set("event.drops", Node(7)));
    CHECK(cfg.write_back_to("event.drops", "received/ops@1.dta:weekend").error().code() ==
          ErrorCode::Refused);
    REQUIRE(cfg.remove_layer("received/ops@1.dta"));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"default", "scratch"});
}

TEST_CASE("a file with blocks reads from text and writes no cache") {
    TempDir dir;
    fs::create_directories(dir.path("cache"));
    const std::string cache_dir = dir.path("cache");
    const std::string path = dir.write("d.dta", "(a 1)\n(#layer x (a 2))\n");
    const std::string plain = dir.write("p.dta", "(b 1)\n");
    std::vector<std::string> notes;
    int writes = 0;
    Config cfg({.edit_mode = false, .tweaks = nullptr, .cache_dir = cache_dir});
    cfg.set_warning_handler([&](const Error& e) { notes.emplace_back(e.message()); });
    cfg.set_writer([&](std::string_view, std::string_view) {
        ++writes;
        return true;
    });
    REQUIRE(cfg.push_layer("d", path));
    REQUIRE(cfg.push_layer("p", plain));
    CHECK(writes == 1);  // p.dta's cache, not d.dta's
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].find("declares layer blocks") != std::string::npos);
    CHECK(cfg.get<int>("a") == 2);
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"d", "d:x", "p", "scratch"});
}

TEST_CASE("a Config refuses a file whose block is malformed, as any parse error") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    Error e = cfg.push_layer("d", dir.write("d.dta", "(a 1)\n(#layer x #include o.dta)\n")).error();
    CHECK(e.message() == "a layer block holds data: no #include inside (#layer ...)");
    CHECK(e.line() == 2);
    e = cfg.push_layer("d", dir.write("e.dta", "(#layer x (a 1))\n(#layer x (a 2))\n")).error();
    CHECK(e.code() == ErrorCode::Refused);
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"scratch"});
}

TEST_CASE("a check reads only the candidate's blocks, on either thread's checker") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    // The stack's top file has a block; a check's session starts from that file's.
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(event (xp 1))\n(#layer x (event (xp 5)))\n")));
    const LayerPolicy live{.data_only = true, .existing_only = true};
    LayerReport plain = ok(cfg.check_layer_text("ops", "(event (xp 2))", live));
    CHECK(plain.ok());
    CHECK(plain.effect.size() == 1);
    const char* const two = "(event (xp 2))\n(#layer (w (disabled)) (event (xp {f})))\n";
    LayerReport mine = ok(cfg.check_layer_text("ops", two, live));
    CHECK(mine.effect.size() == 2);
    CHECK(mine.refusals.size() == 1);
    LayerReport theirs = ok(cfg.checker().check_text("ops", two, live));
    CHECK(theirs.effect.size() == 2);
    REQUIRE(theirs.refusals.size() == 1);
    CHECK(theirs.refusals[0].message() == mine.refusals[0].message());
    // A report that holds is the push's verdict, blocks included.
    LayerReport fine = ok(cfg.check_layer_text("ops", "(#layer w (event (xp 3)))", live));
    CHECK(fine.ok());
    REQUIRE(cfg.push_layer_text("ops", "(#layer w (event (xp 3)))",
                                {.policy = &live, .checked = &fine}));
    CHECK(cfg.get<int>("event.xp") == 3);
}

TEST_CASE("promote into a block is refused, and no cache is baked from a load with blocks") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(a 1)\n(#layer x (a 2))\n")));
    REQUIRE(cfg.set("a", Node(3)));
    const Error e = cfg.promote("scratch", "d:x").error();
    CHECK(e.code() == ErrorCode::Refused);
    CHECK(e.message() == "promote into a layer block is not built: write_back_to it");
    REQUIRE(cfg.write_back_to("a", "d:x"));
    CHECK(dir.read("d.dta") == "(a 1)\n(#layer x (a 3))\n");
    // The cache format has no place for a load's blocks: baking one is refused.
    LoadSession session;
    session.record_for_cache = true;
    auto root = read_file(dir.path("d.dta"), session);
    REQUIRE(root);
    const auto baked = write_cache(**root, dir.path("d.datb"), &session);
    REQUIRE(!baked);
    CHECK(baked.error().message() == "the cache format holds no layer blocks: read the text");
    LoadSession plain;
    plain.record_for_cache = true;
    auto other = read_string("(a 1)", "p.dta", plain);
    CHECK(write_cache(**other, dir.path("p.datb"), &plain));
}

TEST_CASE("an included file's blocks are its layer's, one name per layer") {
    TempDir dir;
    dir.write("inc.dta", "(b 1)\n(#layer x (b 2))\n");
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "#include inc.dta\n(a 1)\n")));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"d", "d:x", "scratch"});
    CHECK(cfg.get<int>("b") == 2);
    const Source s = ok(cfg.source("b"));
    CHECK(s.layer == Symbol("d:x"));
    CHECK(s.file.str().ends_with("inc.dta"));
    CHECK(s.line == 2);
    const Error e =
        cfg.push_layer("e", dir.write("e.dta", "#include inc.dta\n(#layer x (a 2))\n")).error();
    CHECK(e.code() == ErrorCode::Refused);
    CHECK(e.message().find("block 'x' is declared twice (first at ") == 0);
}

#endif
