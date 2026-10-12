// Config: merge, layers, provenance, reload, callbacks, tweaks, live edits, write-back.
#include <doctest.h>
#include <moira/config.hpp>
#include <moira/parse.hpp>
#include <moira/std_interop.hpp>
#include <moira/write.hpp>

#include "alloc_counter.hpp"
#include "temp_dir.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "ok.hpp"

using namespace moira;
using moira::test::ok;
namespace fs = std::filesystem;
using moira::test::diff_lines;
using moira::test::TempDir;

namespace {

Ref<Array> parse(const char* text) {
    auto r = read_string(text, "t.dta");
    REQUIRE(r);
    return *r;
}

std::string str_of(const Array& a) {
    return to_string(a);
}

const char* const kDefault =
    "; engine defaults\n"
    "[kDefaultVolume 0.8]\n"
    "(renderer\n"
    "   (screen_size 1280 720)\n"
    "   (clear_color 0.1 0.1 0.12)\n"
    "   (shadows (enabled 1) (resolution 2048)))\n"
    "(audio\n"
    "   (master_volume kDefaultVolume)   ; from the macro\n"
    "   (channels 64))\n"
    "(box (width 2 (range 0 10) (note \"good values are 2-3\")) (height 1))\n";

const char* const kApp =
    "; app overrides\n"
    "(renderer (screen_size 1920 1080) (shadows (resolution 4096)))\n"
    "(audio (channels 32))\n";

}  // namespace

TEST_CASE("overwrite merge: leaves win, tagged arrays recurse") {
    // The slide's example, with the leaves winning (docs/decisions.md D1).
    auto r = merge(*parse("(a (b 1))"), *parse("(a (b 2) (c 2))"));
    CHECK(str_of(*r) == "((a (b 2) (c 2)))");
    // Values replace values; children are kept and merged.
    r = merge(*parse("(width 2 (range 0 10))"), *parse("(width 3)"));
    CHECK(str_of(*r) == "((width 3 (range 0 10)))");
    r = merge(*parse("(clear_color 0.1 0.1 0.12)"), *parse("(clear_color 1 0)"));
    CHECK(str_of(*r) == "((clear_color 1 0))");
    r = merge(*parse("(points (1 2) (3 4))"), *parse("(points (5 6))"));
    CHECK(str_of(*r) == "((points (5 6)))");  // untagged arrays are values
    r = merge(*parse("(s (a 1) (b 2))"), *parse("(s (b 3))"));
    CHECK(str_of(*r) == "((s (a 1) (b 3)))");
    // Nothing changes: the base itself comes back.
    auto base = parse("(a (b 1))");
    CHECK(merge(*base, *parse("(a)")).get() == base.get());
    CHECK(merge(*base, *Array::make(0)).get() == base.get());
    // Fill-missing keeps existing values.
    r = merge(*parse("(a (b 1))"), *parse("(a (b 2) (c 2))"), MergePolicy::FillMissing);
    CHECK(str_of(*r) == "((a (b 1) (c 2)))");
}

TEST_CASE("a large merge, indexed, gives what small merges give") {
    // Past 32 incoming keys merge indexes the existing ones (D75). Merging the same keys
    // 20 at a time takes the scanning path; both must agree, duplicate tags included.
    std::string base = "(dup 0) (dup 1) ", over = "(dup 2) (dup 3) ";
    for (int i = 0; i < 150; ++i)
        base += "(k" + std::to_string(i) + " " + std::to_string(i) + " (x 1)) ";
    for (int i = 100; i < 250; ++i)
        over += "(k" + std::to_string(i) + " -" + std::to_string(i) + " (y 2)) ";
    over += "(k120 (z 3)) 7";
    const auto b = parse(base.c_str()), o = parse(over.c_str());
    for (const MergePolicy policy : {MergePolicy::Overwrite, MergePolicy::FillMissing}) {
        Ref<Array> step = b;
        for (uint32_t from = 0; from < o->size(); from += 20) {
            const auto part =
                Array::make_copy(o->nodes().subspan(from, std::min<size_t>(20, o->size() - from)));
            step = merge(*step, *part, policy);
        }
        CHECK(str_of(*merge(*b, *o, policy)) == str_of(*step));
    }
}

TEST_CASE("edit mode records where every node is written") {
    const std::string text = "[M 5]\n(a 1 \"two\" 'three four' M\n  (b 2.5))\n";
    LoadSession s;
    SourceMap map;
    s.source_map = &map;
    auto root = read_string(text, "e.dta", s);
    REQUIRE(root);
    const Array& a = *(**root)[0].try_array();
    auto token = [&](size_t i) {
        const SourceSpan* sp = map.span(a, i);
        REQUIRE(sp);
        return text.substr(sp->begin, sp->end - sp->begin);
    };
    CHECK(token(0) == "a");
    CHECK(token(1) == "1");
    CHECK(token(2) == "\"two\"");
    CHECK(token(3) == "'three four'");
    CHECK(map.span(a, 4)->macro == Symbol("M"));  // spliced from macro M
    CHECK(token(4) == "M");
    CHECK(token(5) == "(b 2.5)");
    CHECK(map.span(a, 1)->literal());
    CHECK_FALSE(map.span(a, 4)->literal());
    const SourceSpan* ext = map.extent(a);
    REQUIRE(ext);
    CHECK(text.substr(ext->begin, ext->end - ext->begin) ==
          "(a 1 \"two\" 'three four' M\n  (b 2.5))");
    CHECK(*map.text(Symbol("e.dta")) == text);
    CHECK(map.extent(**root)->end == text.size());

    // Gameplay mode records nothing and builds the same tree.
    auto plain = read_string(text, "e.dta");
    REQUIRE(plain);
    CHECK(structurally_equal(**plain, **root));
}

TEST_CASE("edit mode follows #include and #merge") {
    TempDir dir;
    dir.write("inc.dta", "(from_inc 7)\n");
    dir.write("base.dta", "(s (x 1) (y 2))\n(only_base 3)\n");
    const std::string main =
        dir.write("main.dta", "#include inc.dta\n(s (x 10))\n#merge base.dta\n");
    LoadSession session;
    SourceMap map;
    session.source_map = &map;
    auto root = read_file(main, session);
    REQUIRE(root);
    const Array& r = **root;
    CHECK(to_string(r) == "((from_inc 7) (s (x 10) (y 2)) (only_base 3))");
    const SourceSpan* inc = map.span(r, 0);
    REQUIRE(inc);
    CHECK(inc->file.str().ends_with("inc.dta"));
    // (s ...) was rebuilt by #merge; its spans travel with its nodes.
    const Array& s = *r[1].try_array();
    REQUIRE(map.spans(s));
    CHECK(map.span(s, 1)->file.str().ends_with("main.dta"));  // (x 10)
    CHECK(map.span(s, 2)->file.str().ends_with("base.dta"));  // (y 2), merged in
    CHECK(map.span(r, 2)->file.str().ends_with("base.dta"));
}

TEST_CASE("layers merge, and removing one restores every value") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kDefault)));
    REQUIRE(cfg.push_layer("app", dir.write("app.dta", kApp)));
    CHECK(strings(cfg.layers()) == std::vector<std::string>{"default", "app", "scratch"});
    CHECK(cfg.get<int>("renderer.screen_size.0") == 1920);
    CHECK(cfg.get<int>("renderer.shadows.resolution") == 4096);
    CHECK(cfg.get<int>("renderer.shadows.enabled") == 1);
    CHECK(cfg.get<double>("audio.master_volume") == 0.8);
    CHECK(cfg.get<int>("audio.channels") == 32);

    const Ref<Array> before = cfg.root();
    REQUIRE(
        cfg.push_layer("patch-2026-10-11",
                       parse("(audio (channels 16)) (renderer (clear_color 1 0 0)) (new_key 5)")));
    CHECK(cfg.get<int>("audio.channels") == 16);
    CHECK(cfg.get<double>("renderer.clear_color.1") == 0.0);
    CHECK(cfg.get<int>("new_key") == 5);
    REQUIRE(cfg.remove_layer("patch-2026-10-11"));
    CHECK(structurally_equal(*cfg.root(), *before));
    CHECK_FALSE(cfg.get<int>("new_key"));

    // Removing a middle layer re-merges the rest; popping removes the top.
    const uint64_t epoch = cfg.epoch();
    REQUIRE(cfg.push_layer("top", parse("(audio (channels 8))")));
    REQUIRE(cfg.remove_layer("app"));
    CHECK(cfg.get<int>("audio.channels") == 8);
    CHECK(cfg.get<int>("renderer.screen_size.0") == 1280);
    REQUIRE(cfg.pop_layer());
    CHECK(cfg.get<int>("audio.channels") == 64);
    CHECK(cfg.epoch() == epoch + 3);

    CHECK(error_code_name(cfg.remove_layer("nope").error().code()) == "not found");
    CHECK(cfg.push_layer("default", parse("(x 1)")).error().code() == ErrorCode::Refused);
    CHECK(cfg.push_layer("bad", dir.path("missing.dta")).error().code() == ErrorCode::Io);
}

TEST_CASE("layers above see macros from layers below, and lose them with the layer") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("macros", dir.write("m.dta", "[kSize 7]\n")));
    REQUIRE(cfg.push_layer("use", dir.write("u.dta", "(size kSize)\n")));
    CHECK(cfg.get<int>("size") == 7);
    REQUIRE(cfg.remove_layer("macros"));
    CHECK(cfg.get<Symbol>("size") == Symbol("kSize"));  // now just a symbol
}

TEST_CASE("source names the layer, file, and line") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kDefault)));
    REQUIRE(cfg.push_layer("app", dir.write("app.dta", kApp)));
    REQUIRE(cfg.push_layer("patch", parse("(renderer (clear_color 1 0 0))")));

    Source s = *cfg.source("renderer.screen_size");
    CHECK(s.layer == "app");
    CHECK(s.file.str().ends_with("app.dta"));
    CHECK(s.line == 2);
    s = *cfg.source("renderer.shadows.enabled");
    CHECK(s.layer == "default");
    CHECK(s.file.str().ends_with("default.dta"));
    CHECK(s.line == 6);
    s = *cfg.source("audio.master_volume");
    CHECK(s.layer == "default");
    CHECK(s.line == 8);
    s = *cfg.source("renderer.clear_color.2");
    CHECK(s.layer == "patch");
    CHECK(s.file == Symbol("t.dta"));     // the overlay was parsed from a string named t.dta
    s = *cfg.source("renderer.shadows");  // a container: the topmost layer that has it
    CHECK(s.layer == "app");

#if !MOIRA_SHIPPING
    REQUIRE(cfg.set("renderer.screen_size", std::array<int, 2>{800, 600}));
    s = *cfg.source("renderer.screen_size");
    CHECK(s.layer == "scratch");
    CHECK(cfg.get<int>("renderer.screen_size.1") == 600);
    CHECK(strings(cfg.unsaved()) == std::vector<std::string>{"renderer/screen_size"});
#endif

    CHECK(cfg.source("renderer.nope").error().code() == ErrorCode::NotFound);
    CHECK(cfg.source("a..b").error().code() == ErrorCode::Syntax);
}

#if !MOIRA_SHIPPING  // reload is inert under MOIRA_SHIPPING (tested below)
TEST_CASE("reload sees a file changed on disk") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    dir.write("inc.dta", "(volume 3)\n");
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", "(speed 1)\n#include inc.dta\n")));
    REQUIRE(cfg.push_layer("app", dir.write("app.dta", "(speed 2)\n")));
    CHECK(cfg.get<int>("speed") == 2);
    CHECK(cfg.reload() == false);  // nothing changed

    dir.write("app.dta", "(speed 5)\n");
    CHECK(cfg.reload() == true);
    CHECK(cfg.get<int>("speed") == 5);

    dir.write("inc.dta", "(volume 4)\n");  // an included file counts too
    CHECK(cfg.reload() == true);
    CHECK(cfg.get<int>("volume") == 4);

    // A broken edit changes nothing, and the error says where.
    const Ref<Array> good = cfg.root();
    dir.write("app.dta", "(speed 6\n");
    auto r = cfg.reload();
    REQUIRE_FALSE(r);
    CHECK(r.error().file().str().ends_with("app.dta"));
    CHECK(cfg.root().get() == good.get());
    CHECK(cfg.get<int>("speed") == 5);
    dir.write("app.dta", "(speed 7)\n");
    CHECK(cfg.reload() == true);
    CHECK(cfg.get<int>("speed") == 7);
    // The old tree is still readable by anyone who held it.
    CHECK(good->get<int>("speed") == 5);
}

TEST_CASE("reload callbacks fire per group; punting rebuilds, migrating does not") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(renderer (msaa 4)) (audio (channels 8))\n")));
    int renderer_rebuilds = 0, audio_rebuilds = 0, audio_migrations = 0;
    cfg.on_reload("renderer", [&] { ++renderer_rebuilds; });  // punt by default: no code
    cfg.on_reload(
        "audio", [&] { ++audio_rebuilds; },
        [&] {
            ++audio_migrations;
            return cfg.get<int>("audio.channels") == 16 ? Reload::Migrated : Reload::Punt;
        });

    dir.write("d.dta", "(renderer (msaa 8)) (audio (channels 8))\n");
    REQUIRE(cfg.reload());
    CHECK(renderer_rebuilds == 1);
    CHECK(audio_migrations == 0);  // its group did not change

    dir.write("d.dta", "(renderer (msaa 8)) (audio (channels 16))\n");
    REQUIRE(cfg.reload());
    CHECK(renderer_rebuilds == 1);
    CHECK(audio_migrations == 1);
    CHECK(audio_rebuilds == 0);  // migrated

    dir.write("d.dta", "(renderer (msaa 8)) (audio (channels 2))\n");
    REQUIRE(cfg.reload());
    CHECK(audio_migrations == 2);
    CHECK(audio_rebuilds == 1);  // migration declined: punt

    // Layer changes fire the same callbacks.
    REQUIRE(cfg.push_layer("p", parse("(renderer (msaa 2))")));
    CHECK(renderer_rebuilds == 2);
    REQUIRE(cfg.pop_layer());
    CHECK(renderer_rebuilds == 3);
}

#endif

TEST_CASE("tweaks bind, rebind, and fall back to their defaults") {
    TweakRegistry reg;
    Tweak<float> width{"box.width", 2.0f, {.range = {0, 10}, .step = 1, .desc = "Box width"}, reg};
    Tweak<int> missing{"box.depth", 9, {}, reg};
    Tweak<std::string> title{"app.title", "untitled", {}, reg};
    Tweak<std::array<float, 3>> tint{"look.tint", {1, 1, 1}, {.range = {0, 1}}, reg};
    CHECK(reg.size() == 4);
    CHECK(float(width) == 2.0f);
    CHECK_FALSE(width.bound());

    int width_changes = 0;
    width.on_change([&] { ++width_changes; });

    TempDir dir;
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kDefault)));
    CHECK(width.bound());
    CHECK(float(width) == 2.0f);
    CHECK(width_changes == 1);  // became bound
    CHECK_FALSE(missing.bound());
    CHECK(int(missing) == 9);
    CHECK(title.get() == "untitled");

    REQUIRE(cfg.push_layer(
        "app", parse("(box (width 3.5)) (app (title \"Demo\")) (look (tint 1 0.5 0))")));
    CHECK(float(width) == 3.5f);
    CHECK(width_changes == 2);
    CHECK(title.get() == "Demo");
    CHECK(tint.value()[1] == 0.5f);
    CHECK(tint.kind() == "color");  // inferred: 3 numbers ranged 0..1

    // Metadata: the data file's wins over the code's, and note exists only in data.
    CHECK(width.range().max == 10);
    CHECK(width.step() == 1);
    CHECK(width.desc() == "Box width");
    CHECK(width.note() == "good values are 2-3");
    CHECK(width.group() == "box");

    REQUIRE(cfg.pop_layer());
    CHECK(float(width) == 2.0f);
    CHECK(title.get() == "untitled");  // absent again: the default
    CHECK_FALSE(title.bound());

    // A tweak created after loading binds at once.
    Tweak<int> height{"box.height", 0, {}, reg};
    CHECK(height.bound());
    CHECK(int(height) == 1);

    // A value of the wrong type warns and keeps the default.
    std::vector<Error> warnings;
    cfg.set_warning_handler([&](const Error& e) { warnings.push_back(e); });
    REQUIRE(cfg.push_layer("bad", parse("(box (width wide))")));
    CHECK(float(width) == 2.0f);
    CHECK_FALSE(width.bound());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message() == "'width' is symbol, not float");
}

TEST_CASE("registering a tweak allocates nothing") {
    TweakRegistry reg;
    test::AllocationScope scope;
    {
        Tweak<float> t{"a.b", 1.0f, {.desc = "d"}, reg};
        CHECK(float(t) == 1.0f);
    }
    if (test::counting_allocations()) CHECK(scope.count() == 0);
    CHECK(reg.size() == 0);
}

TEST_CASE("live edits through tweaks, and write-back to the source line") {
    TweakRegistry reg;
    Tweak<float> width{"box.width", 2.0f, {}, reg};
    TempDir dir;
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kDefault)));
    REQUIRE(cfg.push_layer("app", dir.write("app.dta", kApp)));

#if MOIRA_SHIPPING
    CHECK(width.set(2.7f).error().code() == ErrorCode::Refused);
#else
    REQUIRE(width.set(2.7f));
    CHECK(float(width) == 2.7f);
    CHECK(width.source()->layer == "scratch");
    CHECK(cfg.get<double>("box.width") == 2.7);  // written as the float's decimal

    const std::string before = dir.read("default.dta");
    REQUIRE(width.write_back());
    const std::string after = dir.read("default.dta");
    CHECK(diff_lines(before, after) ==
          std::vector<std::string>{
              "-(box (width 2 (range 0 10) (note \"good values are 2-3\")) (height 1))",
              "+(box (width 2.7 (range 0 10) (note \"good values are 2-3\")) (height 1))"});
    CHECK(width.source()->layer == "default");  // saved: no longer live
    CHECK(width.source()->line == 10);
    CHECK(cfg.unsaved().empty());
    CHECK(float(width) == 2.7f);
    CHECK(width.note() == "good values are 2-3");

    // Writing back an unchanged value changes nothing.
    REQUIRE(cfg.write_back("box.width"));
    CHECK(dir.read("default.dta") == after);

    // An override in a higher layer is written there, as one token.
    REQUIRE(cfg.set("renderer.screen_size.1", Node(1200)));
    REQUIRE(cfg.write_back("renderer.screen_size.1"));
    CHECK(dir.read("app.dta") ==
          "; app overrides\n(renderer (screen_size 1920 1200) (shadows (resolution 4096)))\n"
          "(audio (channels 32))\n");
    CHECK(cfg.unsaved().empty());
#endif
}

#if !MOIRA_SHIPPING
TEST_CASE("write-back refuses what it cannot edit exactly") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kDefault)));
    REQUIRE(cfg.push_layer("patch", parse("(renderer (clear_color 1 0 0))")));

    // A value spliced in from a macro.
    REQUIRE(cfg.set("audio.master_volume", Node(0.5)));
    auto r = cfg.write_back("audio.master_volume");
    REQUIRE_FALSE(r);
    CHECK(r.error().code() == ErrorCode::Refused);
    CHECK(r.error().message() ==
          "'audio/master_volume' comes from macro 'kDefaultVolume'; edit the macro's definition "
          "instead");
    CHECK(r.error().file().str().ends_with("default.dta"));
    CHECK(r.error().line() == 8);
    CHECK(cfg.unsaved().size() == 1);  // still live

    // A value from an overlay layer.
    REQUIRE(cfg.set("renderer.clear_color.0", Node(0.5)));
    r = cfg.write_back("renderer.clear_color");
    CHECK(r.error().message() ==
          "'renderer/clear_color' comes from layer 'patch', which has no source file");

    // A file edited since it was read.
    const std::string edited = dir.read("default.dta") + "; edited by hand\n";
    dir.write("default.dta", edited);
    REQUIRE(cfg.set("audio.channels", Node(4)));
    r = cfg.write_back("audio.channels");
    CHECK(r.error().code() == ErrorCode::Refused);
    CHECK(
        r.error().message().ends_with("has changed since it was read; reload before writing back"));
    CHECK(dir.read("default.dta") == edited);  // untouched
    REQUIRE(cfg.reload());
    REQUIRE(cfg.write_back("audio.channels"));
    CHECK(dir.read("default.dta").find("(channels 4))") != std::string::npos);

    // A value no layer's file has.
    REQUIRE(cfg.set("fresh.key", Node(1)));
    CHECK(cfg.write_back("fresh.key").error().code() == ErrorCode::Refused);
}

TEST_CASE("write-back into an array assembled by #merge is refused") {
    TempDir dir;
    dir.write("base.dta", "(s (w 1 (range 0 2)))\n");
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("main", dir.write("main.dta", "(s (w 5))\n#merge base.dta\n")));
    // (w 5) gained (range 0 2) from base.dta, so it was rebuilt; its values are main.dta's
    // tokens, carried along with their spans.
    REQUIRE(cfg.set("s.w", Node(6)));
    REQUIRE(cfg.write_back("s.w"));
    CHECK(dir.read("main.dta") == "(s (w 6))\n#merge base.dta\n");
}

TEST_CASE("write_back_to writes into another layer, adding the key if needed") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kDefault)));
    REQUIRE(cfg.push_layer("local", dir.write("local.dta", "; my machine\n(box (height 3))")));
    REQUIRE(cfg.set("box.width", Node(4)));
    REQUIRE(cfg.write_back_to("box.width", "local"));
    CHECK(dir.read("local.dta") == "; my machine\n(box (height 3) (width 4))");
    CHECK(cfg.source("box.width")->layer == "local");
    CHECK(cfg.unsaved().empty());

    REQUIRE(cfg.set("audio.channels", Node(2)));
    REQUIRE(cfg.write_back_to("audio.channels", "local"));
    CHECK(dir.read("local.dta") ==
          "; my machine\n(box (height 3) (width 4))\n(audio (channels 2))\n");
    CHECK(cfg.get<int>("audio.channels") == 2);

    // Into a layer below the one that supplies the value: written, but still shadowed, so
    // the live edit stays.
    REQUIRE(cfg.set("box.height", Node(9)));
    REQUIRE(cfg.write_back_to("box.height", "default"));
    CHECK(dir.read("default.dta").find("(height 9))") != std::string::npos);
    CHECK(strings(cfg.unsaved()) == std::vector<std::string>{"box/height"});
    CHECK(cfg.get<int>("box.height") == 9);
}

TEST_CASE("freeze ends the initial load") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(a 1)\n")));
    cfg.freeze();
    CHECK(cfg.push_layer("p", parse("(a 2)")).error().code() == ErrorCode::Refused);
    CHECK(cfg.set("a", Node(3)).error().code() == ErrorCode::Refused);
    dir.write("d.dta", "(a 5)\n");
    CHECK(cfg.reload() == false);
    CHECK(cfg.get<int>("a") == 1);

    Config live({.live_layers = true, .tweaks = nullptr});
    REQUIRE(live.push_layer("d", dir.path("d.dta")));
    live.freeze();
    REQUIRE(live.push_layer("patch", parse("(a 2)")));  // the explicit live-ops path
    CHECK(live.get<int>("a") == 2);
}
#endif

#if MOIRA_SHIPPING
TEST_CASE("under MOIRA_SHIPPING reload is inert and dev_only tweaks keep their defaults") {
    TweakRegistry reg;
    Tweak<int> cheat{"debug.god_mode", 0, {.dev_only = true}, reg};
    Tweak<int> speed{"player.speed", 1, {}, reg};
    TempDir dir;
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(debug (god_mode 1)) (player (speed 5))\n")));
    CHECK(int(cheat) == 0);
    CHECK_FALSE(cheat.bound());
    CHECK(int(speed) == 5);
    dir.write("d.dta", "(debug (god_mode 1)) (player (speed 9))\n");
    CHECK(cfg.reload() == false);
    CHECK(int(speed) == 5);
    CHECK(speed.set(7).error().code() == ErrorCode::Refused);
    CHECK(cfg.write_back("player.speed").error().code() == ErrorCode::Refused);
}
#endif

TEST_CASE("the short forms in one place") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    ok(cfg.load(dir.write("default.dta", kDefault)));
    ok(cfg.merge(dir.write("app.dta", kApp)));
    CHECK(cfg.get<int>("audio.channels") == 32);
    CHECK(cfg.get<int>("audio.missing", 3) == 3);
    CHECK(ok(cfg.source("audio.channels")).file.str().ends_with("app.dta"));
    CHECK_FALSE(cfg.push_layer("x", dir.path("nope.dta")));
    CHECK_FALSE(cfg.get<int>("audio.nope"));
    if constexpr (kShipping) {
        CHECK(cfg.set("audio.channels", 4).error().code() == ErrorCode::Refused);
    } else {
        ok(cfg.set("audio.channels", 4));
        ok(cfg.write_back("audio.channels"));
        CHECK(dir.read("app.dta").find("(audio (channels 4))") != std::string::npos);
        CHECK(ok(cfg.reload()) == false);
    }
}

#if !MOIRA_SHIPPING
namespace {
// Every path in `a` that ends on an array with values: menu/item/price, ...
void value_paths(const Array& a, const std::string& prefix, std::vector<std::string>& out) {
    for (const Array& child : a.subarrays()) {
        const auto tag = child.empty() ? std::nullopt : child[0].try_sym();
        if (!tag) continue;
        const std::string path =
            (prefix.empty() ? "" : prefix + "/") + "'" + std::string(tag->str()) + "'";
        bool values = false;
        for (size_t i = 1; i < child.size(); ++i) {
            const Array* c = child[i].try_array();
            values = values || !c || c->empty() || !(*c)[0].try_sym();
        }
        if (values) out.push_back(path);
        value_paths(child, path, out);
    }
}
}  // namespace

TEST_CASE("every corpus config value writes back as one line, or is refused with a location") {
    TempDir dir;
    for (const char* name : {"config_default.dta", "config_app.dta", "macros.dta"}) {
        std::ifstream in(fs::path(MOIRA_CORPUS_DIR) / name, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        dir.write(name, ss.str());
    }
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("app", dir.path("config_app.dta")));
    std::vector<std::string> paths;
    value_paths(*cfg.root(), "", paths);
    REQUIRE(paths.size() >= 10);
    size_t written = 0, refused_count = 0;
    for (const std::string& path_text : paths) {
        CAPTURE(path_text);
        const Path path(path_text);
        REQUIRE(cfg.set(path, Node(4242)));
        auto source = cfg.source(path);
        REQUIRE(source);
        std::map<std::string, std::string> before;
        for (const char* name : {"config_default.dta", "config_app.dta", "macros.dta"})
            before[name] = dir.read(name);
        auto r = cfg.write_back(path);
        if (!r) {
            CHECK(r.error().code() == ErrorCode::Refused);
            CHECK_FALSE(r.error().file().empty());
            CHECK(r.error().line() > 0);
            ++refused_count;
            continue;
        }
        size_t changed_lines = 0;
        for (const auto& [name, text] : before)
            changed_lines += diff_lines(text, dir.read(name)).size() / 2;
        CHECK(changed_lines == 1);
        CHECK(cfg.get<int>(path) == 4242);
        const auto unsaved = cfg.unsaved();  // refused paths stay live; this one is saved
        CHECK(std::find(unsaved.begin(), unsaved.end(), Symbol(to_string(path))) == unsaved.end());
        ++written;
    }
    CHECK(written >= 8);
    CHECK(refused_count >= 2);  // kDefaultVolume and kMaxPlayers are macros
    MESSAGE("wrote " << written << ", refused " << refused_count);
}
#endif

TEST_CASE("cache_dir: file layers load through caches outside edit mode") {
    TempDir dir;
    fs::create_directories(dir.path("cache"));
    const std::string base =
        dir.write("base.dta", "#include tuning.dta\n#define WIDE\n(screen (width 1280))");
    dir.write("tuning.dta", "(speed 3)");
    const std::string top = dir.write("top.dta", "#ifdef WIDE (screen (width 2560)) #endif");
    const std::string cache_dir = dir.path("cache");
    int writes = 0;
    auto make = [&] {
        auto cfg = std::make_unique<Config>(
            ConfigOptions{.edit_mode = false, .tweaks = nullptr, .cache_dir = cache_dir});
        cfg->set_writer([&](std::string_view path, std::string_view text) {
            ++writes;
            std::ofstream(std::string(path), std::ios::binary) << text;
            return true;
        });
        return cfg;
    };

    auto first = make();
    REQUIRE(first->push_layer("base", base));
    REQUIRE(first->push_layer("top", top));  // sees WIDE, replayed or parsed
    CHECK(first->get<int>("screen.width") == 2560);
    CHECK(first->get<int>("speed") == 3);
    if constexpr (kShipping) {  // shipping writes no caches, and reads the text without one
        CHECK(writes == 0);
        return;
    }
    CHECK(writes == 2);  // one cache per layer
    CHECK(std::distance(fs::directory_iterator(cache_dir), fs::directory_iterator()) == 2);

    // Another Config reads both layers from their caches, with the same values and sources.
    auto second = make();
    REQUIRE(second->push_layer("base", base));
    REQUIRE(second->push_layer("top", top));
    CHECK(writes == 2);
    CHECK(second->get<int>("screen.width") == 2560);
    CHECK(second->get<int>("speed") == 3);
    CHECK(ok(second->source("speed")).file == Symbol(dir.path("tuning.dta")));
    CHECK(ok(second->source("screen.width")).layer == Symbol("top"));

    if constexpr (!kShipping) {
        // A changed include is read from text on reload, and its layer's cache rewritten.
        dir.write("tuning.dta", "(speed 4)");
        REQUIRE(second->reload());
        CHECK(second->get<int>("speed") == 4);
        CHECK(writes >= 3);
        const int after = writes;
        auto third = make();
        REQUIRE(third->push_layer("base", base));
        CHECK(third->get<int>("speed") == 4);
        CHECK(writes == after);
    }

    // Edit mode reads text, and writes no cache.
    Config edit({.edit_mode = true, .tweaks = nullptr, .cache_dir = cache_dir});
    edit.set_writer([&](std::string_view, std::string_view) {
        ++writes;
        return true;
    });
    const int before = writes;
    REQUIRE(edit.push_layer("base", base));
    CHECK(writes == before);
}

TEST_CASE("Config forwards the one-way rule's warnings") {
    TempDir dir;
    dir.write("task.dta", "(pack boss_task (level task))\n#define PHASES (3)");
    const std::string game =
        dir.write("game.dta", "(pack rules (level game))\n#require task.dta\n(b PHASES)");
    Config cfg({.tweaks = nullptr});
    cfg.session().set_pack_levels({"game", "task"});
    std::vector<std::string> warnings;
    cfg.set_warning_handler([&](const Error& e) { warnings.emplace_back(to_string(e)); });
    REQUIRE(cfg.push_layer("game", game));
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("uses macro 'PHASES'") != std::string::npos);
}
