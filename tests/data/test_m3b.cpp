// M3b: reload telemetry, layer options and toggles, scratch, layer effect and diff,
// write_back_to insertion, promote, and snapshots (spec §4, docs/decisions.md D42).
#include <doctest.h>
#include <moira/config.hpp>
#include <moira/parse.hpp>
#include <moira/write.hpp>

#include "alloc_counter.hpp"
#include "temp_dir.hpp"

#include <string>
#include <vector>
#include "ok.hpp"

using namespace moira;
using moira::test::diff_lines;
using moira::test::ok;
using moira::test::TempDir;

namespace {

// Advances a fixed step per call, so every stage's duration is a known multiple of it.
struct FakeClock {
    uint64_t t = 0;
    uint64_t step = 10;
    static uint64_t tick(void* self) {
        auto* c = static_cast<FakeClock*>(self);
        return c->t += c->step;
    }
};

[[maybe_unused]] Ref<Array> parse(const char* text) {
    auto r = read_string(text, "overlay.dta");
    REQUIRE(r);
    return *r;
}

[[maybe_unused]] std::vector<ReloadRecord> history(const Config& cfg) {
    std::vector<ReloadRecord> out;
    cfg.each_reload([&](const ReloadRecord& r) { out.push_back(r); });
    return out;
}

const char* const kBase =
    "; shared defaults\n"
    "(box\n"
    "   (width 2 (note \"2-3 reads well\"))   ; tuned in playtest 4\n"
    "   (height 1))\n"
    "(audio (volume 0.8) (channels 64))\n";

}  // namespace

// --- Change 1: reload telemetry ---

#if !MOIRA_SHIPPING
TEST_CASE("a reload's six stages are timed exactly with a fake time source") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", "(speed 1)\n")));
    dir.write("default.dta", "(speed 2)\n");
    FakeClock clock;
    cfg.set_time_source(FakeClock::tick, &clock);
    REQUIRE(cfg.reload({.trigger = "watcher"_sym, .origin_ns = 3}) == true);

    // One file: begin, scan end, read start and end, parse end, merge, bind, notify.
    const auto r = cfg.last_reload();
    REQUIRE(r);
    CHECK(r->trigger == Symbol("watcher"));
    CHECK(r->origin_ns == 3);
    CHECK(r->start_ns == 10);
    CHECK(r->scan_ns == 10);
    CHECK(r->read_ns == 10);
    CHECK(r->parse_ns == 20);
    CHECK(r->merge_ns == 10);
    CHECK(r->bind_ns == 10);
    CHECK(r->notify_ns == 10);
    CHECK(r->end_ns == 80);  // the end of the notify stage: the stages tile the reload
    CHECK(r->files_reloaded == 1);
    CHECK(r->first_file.str().ends_with("default.dta"));
    CHECK(r->outcome == ReloadOutcome::Ok);
    CHECK(r->epoch == cfg.epoch());

    REQUIRE(cfg.reload() == false);
    CHECK(cfg.last_reload()->outcome == ReloadOutcome::Unchanged);
    CHECK(cfg.last_reload()->trigger == Symbol("reload"));
    CHECK(cfg.last_reload()->epoch == r->epoch);
}

TEST_CASE("telemetry adds no allocation per reload once the history exists") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", "(speed 1)\n")));
    auto allocations_for = [&](size_t history) {
        cfg.set_reload_history(history);
        for (int i = 0; i < 3; ++i) (void)cfg.reload();  // fills (and wraps) the ring
        test::AllocationScope scope;
        for (int i = 0; i < 20; ++i) (void)cfg.reload();
        return scope.count();
    };
    if (!test::counting_allocations()) return;
    const size_t without = allocations_for(0);
    const size_t with = allocations_for(2);
    CHECK(with == without);
    CHECK(history(cfg).size() == 2);
}

TEST_CASE("the history ring wraps, keeping the newest records in order") {
    Config cfg({.tweaks = nullptr});
    cfg.set_reload_history(3);
    for (int i = 0; i < 5; ++i) REQUIRE(cfg.push_layer("l" + std::to_string(i), parse("(x 1)")));
    const auto h = history(cfg);
    REQUIRE(h.size() == 3);
    CHECK(h[0].epoch + 1 == h[1].epoch);
    CHECK(h[1].epoch + 1 == h[2].epoch);
    CHECK(h[2].epoch == cfg.epoch());
    CHECK(h[2].trigger == Symbol("layer"));
    CHECK(h[2].scan_ns == 0);  // a layer change reads no files
    CHECK(h[2].read_ns == 0);
    CHECK(h[2].parse_ns == 0);
}

TEST_CASE("a parse error records ParseError and changes nothing") {
    TempDir dir;
    TweakRegistry reg;
    Tweak<int> speed{"speed", 0, {}, reg};
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", "(speed 1)\n")));
    dir.write("default.dta", "(speed 2\n");
    FakeClock clock;
    cfg.set_time_source(FakeClock::tick, &clock);
    CHECK_FALSE(cfg.reload());
    const auto r = cfg.last_reload();
    REQUIRE(r);
    CHECK(r->outcome == ReloadOutcome::ParseError);
    CHECK(r->bind_ns == 0);
    CHECK(r->notify_ns == 0);
    CHECK(int(speed) == 1);
}

TEST_CASE("records count migrations and punts, keep the origin, and reach the host") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(a (x 1)) (b (x 1)) (c (x 1))\n")));
    int rebuilds = 0;
    cfg.on_reload("a", [&] { ++rebuilds; });  // punts
    cfg.on_reload("b", [&] { ++rebuilds; }, [] { return Reload::Migrated; });
    cfg.on_reload("c", [&] { ++rebuilds; }, [] { return Reload::Punt; });
    std::vector<ReloadRecord> seen;
    const auto id = cfg.on_reload_complete([&](const ReloadRecord& r) { seen.push_back(r); });
    dir.write("d.dta", "(a (x 2)) (b (x 2)) (c (x 2))\n");
    REQUIRE(cfg.reload({.trigger = "console"_sym, .origin_ns = 12345}));
    const auto r = cfg.last_reload();
    CHECK(r->migrated == 1);
    CHECK(r->punted == 2);
    CHECK(rebuilds == 2);
    CHECK(r->origin_ns == 12345);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].trigger == Symbol("console"));
    cfg.remove_on_reload(id);
    REQUIRE(cfg.push_layer("p", parse("(a (x 3))")));
    CHECK(seen.size() == 1);
}
#else
TEST_CASE("under MOIRA_SHIPPING telemetry is empty but its API stays") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(a 1)\n")));
    CHECK_FALSE(cfg.last_reload());
    int visited = 0;
    cfg.each_reload([&](const ReloadRecord&) { ++visited; });
    CHECK(visited == 0);
    CHECK(cfg.toggle_layer("d").error().code() == ErrorCode::Refused);
    CHECK(cfg.set("a", Node(2)).error().code() == ErrorCode::Refused);
    CHECK(cfg.promote("d", "d").error().code() == ErrorCode::Refused);
    CHECK(cfg.layer_effect("d")->size() == 1);  // tooling still works
}
#endif

// --- Change 2: layers, scratch, effect, diff, write_back_to, promote ---

#if !MOIRA_SHIPPING
TEST_CASE("toggling a layer off and on restores every value and source exactly") {
    TempDir dir;
    TweakRegistry reg;
    Tweak<float> width{"box.width", 0, {}, reg};
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    REQUIRE(cfg.push_layer("experiment",
                           dir.write("exp.dta", "(box (width 3)) (audio (volume 0.5))\n")));
    const Ref<Array> with = cfg.root();
    const Source s_width = *cfg.source("box.width");
    CHECK(s_width.layer == "experiment");
    CHECK(float(width) == 3.0f);

    CHECK(cfg.toggle_layer("experiment") == false);
    CHECK_FALSE(cfg.layer_enabled("experiment"));
    CHECK(float(width) == 2.0f);
    CHECK(cfg.source("box.width")->layer == "default");  // never a disabled layer
    CHECK(cfg.get<double>("audio.volume") == 0.8);
    CHECK(cfg.last_reload()->trigger == Symbol("layer"));

    CHECK(cfg.toggle_layer("experiment") == true);
    CHECK(structurally_equal(*cfg.root(), *with));
    const Source back = *cfg.source("box.width");
    CHECK(back.layer == s_width.layer);
    CHECK(back.file == s_width.file);
    CHECK(back.line == s_width.line);
    CHECK(float(width) == 3.0f);
    CHECK(cfg.layers() == std::vector<std::string>{"default", "experiment", "scratch"});
    CHECK(cfg.toggle_layer("nope").error().code() == ErrorCode::NotFound);

    // A layer pushed disabled keeps its place but contributes nothing.
    REQUIRE(cfg.push_layer("off", parse("(box (width 9))"), {.enabled = false}));
    CHECK(float(width) == 3.0f);
}

TEST_CASE("an optional layer with a missing file is empty, and appears on reload") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    REQUIRE(cfg.push_layer("local", dir.path("local/sam.dta"), {.optional = true}));
    CHECK(cfg.get<int>("box.height") == 1);
    CHECK(cfg.push_layer("bad", dir.path("missing.dta")).error().code() == ErrorCode::Io);
    dir.write("local/sam.dta", "(box (height 5))\n");
    REQUIRE(cfg.reload() == true);
    CHECK(cfg.get<int>("box.height") == 5);
    CHECK(cfg.source("box.height")->layer == "local");
}

TEST_CASE("set on a scalar already in scratch allocates nothing and touches only its tweak") {
    TempDir dir;
    TweakRegistry reg;
    Tweak<float> width{"box.width", 0, {}, reg};
    Tweak<int> height{"box.height", 0, {}, reg};
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    int width_changes = 0, height_changes = 0;
    width.on_change([&] { ++width_changes; });
    height.on_change([&] { ++height_changes; });

    REQUIRE(cfg.set("box.width", Node(2.5)));  // first set: creates the scratch entry
    CHECK(float(width) == 2.5f);
    const uint64_t epoch = cfg.epoch();
    const auto record = cfg.last_reload();
    test::AllocationScope scope;
    REQUIRE(cfg.set("box.width", Node(2.75)));
    const size_t allocations = scope.count();
    if (test::counting_allocations()) CHECK(allocations == 0);
    CHECK(float(width) == 2.75f);
    CHECK(width_changes == 2);
    CHECK(height_changes == 0);
    CHECK(cfg.epoch() == epoch);  // no full re-merge
    CHECK(cfg.last_reload()->epoch == record->epoch);
    CHECK(cfg.get<double>("box.width") == 2.75);  // lookups see scratch first
    CHECK(cfg.source("box.width")->layer == "scratch");
    CHECK(cfg.root()->get<double>("box.width") == 2.0);  // root(): as of the last merge

    // Toggling scratch is a full re-merge like any layer.
    CHECK(cfg.toggle_layer("scratch") == false);
    CHECK(float(width) == 2.0f);
    CHECK(cfg.get<double>("box.width") == 2.0);
    CHECK(cfg.toggle_layer("scratch") == true);
    CHECK(float(width) == 2.75f);
    REQUIRE(cfg.clear_scratch("box.width"));
    CHECK(float(width) == 2.0f);
    CHECK(cfg.unsaved().empty());
}

TEST_CASE("set then write_back: one line, comments and note kept, scratch emptied") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    REQUIRE(cfg.set("box.width", Node(2.4)));
    CHECK(cfg.unsaved() == std::vector<std::string>{"box/width"});
    const std::string before = dir.read("default.dta");
    auto report = cfg.write_back("box.width");
    REQUIRE(report);
    CHECK(diff_lines(before, dir.read("default.dta")) ==
          std::vector<std::string>{
              "-   (width 2 (note \"2-3 reads well\"))   ; tuned in playtest 4",
              "+   (width 2.4 (note \"2-3 reads well\"))   ; tuned in playtest 4"});
    REQUIRE(report->written.size() == 1);
    CHECK(report->written[0].file.str().ends_with("default.dta"));
    CHECK(report->written[0].line == 3);
    CHECK(cfg.unsaved().empty());
    const Source s = *cfg.source("box.width");
    CHECK(s.layer == "default");
    CHECK(s.line == 3);
    CHECK(cfg.last_reload()->trigger == Symbol("write_back"));
    CHECK(cfg.last_reload()->files_reloaded == 1);
}

TEST_CASE("write_back_to creates an absent personal file, or inserts beside siblings") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    REQUIRE(cfg.push_layer("local", dir.path("local/sam.dta"), {.optional = true}));
    REQUIRE(cfg.set("box.width", Node(4)));
    auto r = cfg.write_back_to("box.width", "local");
    REQUIRE(r);
    CHECK(dir.read("local/sam.dta") == "(box (width 4))\n");
    CHECK(cfg.source("box.width")->layer == "local");
    CHECK(cfg.unsaved().empty());

    // Into a populated file: a multi-line ancestor gets a new line indented like its
    // last child; nothing else changes.
    const std::string mine =
        "; mine\n"
        "(audio\n"
        "   (channels 2)   ; quiet\n"
        "   (rate 44100))\n"
        "(box (width 4))\n";
    dir.write("local/sam.dta", mine);
    REQUIRE(cfg.reload());
    REQUIRE(cfg.set("audio.volume", Node(0.25)));
    REQUIRE(cfg.write_back_to("audio.volume", "local"));
    CHECK(dir.read("local/sam.dta") ==
          "; mine\n"
          "(audio\n"
          "   (channels 2)   ; quiet\n"
          "   (rate 44100)\n"
          "   (volume 0.25))\n"
          "(box (width 4))\n");
    // A one-line ancestor gets the entry on its line.
    REQUIRE(cfg.set("box.height", Node(7)));
    REQUIRE(cfg.write_back_to("box.height", "local"));
    CHECK(dir.read("local/sam.dta").ends_with("(box (width 4) (height 7))\n"));
    // A missing positional segment is refused, located.
    auto bad = cfg.write_back_to("box.width.3", "local");
    CHECK_FALSE(bad);

    // A multi-line ancestor whose last child is followed by a comment line.
    dir.write("local/sam.dta", "(audio\n   (channels 2) ; quiet\n)\n");
    REQUIRE(cfg.reload());
    REQUIRE(cfg.set("audio.volume", Node(0.5)));
    REQUIRE(cfg.write_back_to("audio.volume", "local"));
    CHECK(dir.read("local/sam.dta") == "(audio\n   (channels 2) ; quiet\n   (volume 0.5)\n)\n");
}

TEST_CASE("write-back refuses a file changed on disk since it was read") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    REQUIRE(cfg.push_layer("local", dir.write("local.dta", "(box (height 3))\n")));
    dir.write("local.dta", "(box (height 3)) ; edited by hand\n");
    REQUIRE(cfg.set("box.width", Node(5)));
    auto r = cfg.write_back_to("box.width", "local");
    REQUIRE_FALSE(r);
    CHECK(r.error().code() == ErrorCode::Refused);
    CHECK(r.error().file().str().ends_with("local.dta"));
    CHECK(dir.read("local.dta") == "(box (height 3)) ; edited by hand\n");
}

TEST_CASE("promote writes all entries or none, and reports every refusal") {
    TempDir dir;
    dir.write("net.dta", "(net (port 80))\n");
    const std::string base = "#include net.dta\n(box (width 2))\n";
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", base)));
    REQUIRE(cfg.push_layer("exp", dir.write("exp.dta",
                                            "(box (width 3) (depth 4))\n"
                                            "(net (timeout 5))\n"
                                            "(net2 (x 1))\n")));
    auto r = cfg.promote("exp", "default");
    REQUIRE(r);
    CHECK_FALSE(r->ok());
    REQUIRE(r->refused.size() == 1);  // net's array comes from net.dta, not default.dta
    CHECK(r->refused[0].code() == ErrorCode::Refused);
    CHECK(r->refused[0].file().str().ends_with("net.dta"));
    CHECK(r->refused[0].line() == 1);
    CHECK(r->written.empty());
    CHECK(dir.read("default.dta") == base);  // nothing written

    // Without the unwritable entry, every entry lands, as one reviewable diff.
    dir.write("exp.dta", "(box (width 3) (depth 4))\n(net2 (x 1))\n");
    REQUIRE(cfg.reload());
    r = cfg.promote("exp", "default");
    REQUIRE(r);
    CHECK(r->ok());
    CHECK(r->written.size() == 3);
    CHECK(dir.read("default.dta") == "#include net.dta\n(box (width 3) (depth 4))\n(net2 (x 1))\n");
    CHECK(cfg.layers() == std::vector<std::string>{"default", "exp", "scratch"});  // exp stays
    REQUIRE(cfg.remove_layer("exp"));
    CHECK(cfg.get<int>("box.depth") == 4);
    CHECK(cfg.source("box.depth")->layer == "default");
}

TEST_CASE("layer_effect reports below and shadowed in a three-layer stack") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("base", parse("(a 1) (b 1) (c 1)")));
    REQUIRE(cfg.push_layer("mid", parse("(a 2) (b 2) (d (e 2))")));
    REQUIRE(cfg.push_layer("top", parse("(b 3)")));
    auto effect = cfg.layer_effect("mid");
    REQUIRE(effect);
    REQUIRE(effect->size() == 3);
    const LayerEffectEntry& a = (*effect)[0];
    CHECK(a.path == "a");
    CHECK(a.value.try_int() == 2);
    REQUIRE(a.below);
    CHECK(a.below->try_int() == 1);
    CHECK(a.below_source->layer == "base");
    CHECK_FALSE(a.shadowed);
    const LayerEffectEntry& b = (*effect)[1];
    CHECK(b.path == "b");
    CHECK(b.shadowed);  // top overrides it
    CHECK(b.below->try_int() == 3);
    CHECK(b.below_source->layer == "top");
    const LayerEffectEntry& e = (*effect)[2];
    CHECK(e.path == "d.e");
    CHECK_FALSE(e.below);  // nothing else sets it
    CHECK_FALSE(e.below_source);
    CHECK(e.source.layer == "mid");
    CHECK(cfg.layer_effect("nope").error().code() == ErrorCode::NotFound);
}

TEST_CASE("diff compares two layers' own values, for A/B") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(
        cfg.push_layer("a", parse("(boss (cadence 1.2) (hp 100)) (only_a 1)"), {.enabled = false}));
    REQUIRE(cfg.push_layer("b", parse("(boss (cadence 0.9) (hp 100) (size 1 2))")));
    auto d = cfg.diff("a", "b");
    REQUIRE(d);
    REQUIRE(d->size() == 3);
    CHECK((*d)[0].path == "boss.cadence");
    CHECK((*d)[0].a->try_float() == 1.2);
    CHECK((*d)[0].b->try_float() == 0.9);
    CHECK((*d)[0].source_a->layer == "a");
    CHECK((*d)[1].path == "only_a");
    CHECK_FALSE((*d)[1].b);
    CHECK((*d)[2].path == "boss.size");
    CHECK_FALSE((*d)[2].a);
    CHECK(to_string(*(*d)[2].b) == "(1 2)");
}

// --- Change 3: snapshot and restore ---

TEST_CASE("restore returns every value and source to the snapshot") {
    TempDir dir;
    TweakRegistry reg;
    Tweak<float> width{"box.width", 0, {}, reg};
    Tweak<int> height{"box.height", 0, {}, reg};
    Tweak<double> volume{"audio.volume", 0, {}, reg};
    Config cfg({.tweaks = &reg});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    REQUIRE(cfg.push_layer("exp", parse("(audio (volume 0.6))")));
    REQUIRE(cfg.set("box.height", Node(4)));
    const ConfigSnapshot snap = cfg.snapshot();
    REQUIRE(cfg.restore(snap));  // a full merge, so root() includes the fast-path set
    const std::vector<const char*> paths{"box.width", "box.height", "audio.volume",
                                         "audio.channels"};
    std::vector<Source> sources;
    for (const char* p : paths) sources.push_back(*cfg.source(p));
    const Ref<Array> root = cfg.root();
    int box_rebuilds = 0, audio_rebuilds = 0;
    cfg.on_reload("box", [&] { ++box_rebuilds; });
    cfg.on_reload("audio", [&] { ++audio_rebuilds; });

    REQUIRE(cfg.push_layer("more", parse("(box (width 8))")));
    REQUIRE(cfg.toggle_layer("exp"));
    REQUIRE(cfg.set("box.width", Node(9)));
    REQUIRE(cfg.set("box.height", Node(10)));
    REQUIRE(cfg.set("audio.channels", Node(2)));
    box_rebuilds = audio_rebuilds = 0;
    const uint64_t epoch = cfg.epoch();

    REQUIRE(cfg.restore(snap));
    CHECK(box_rebuilds == 1);
    CHECK(audio_rebuilds == 1);
    CHECK(cfg.epoch() == epoch + 1);
    CHECK(cfg.last_reload()->trigger == Symbol("restore"));
    CHECK(structurally_equal(*cfg.root(), *root));
    CHECK(float(width) == 2.0f);
    CHECK(int(height) == 4);
    CHECK(double(volume) == 0.6);
    for (size_t i = 0; i < paths.size(); ++i) {
        CAPTURE(paths[i]);
        const Source s = *cfg.source(paths[i]);
        CHECK(s.layer == sources[i].layer);
        CHECK(s.file == sources[i].file);
        CHECK(s.line == sources[i].line);
    }
    CHECK(cfg.layers() == std::vector<std::string>{"default", "exp", "scratch"});
    CHECK(cfg.unsaved() == std::vector<std::string>{"box/height"});
}

TEST_CASE("restore never reads disk, and write-back of restored values checks the file") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", kBase)));
    const ConfigSnapshot snap = cfg.snapshot();
    dir.write("default.dta", "(box (width 7))\n");
    REQUIRE(cfg.reload());
    CHECK(cfg.get<int>("box.width") == 7);
    REQUIRE(cfg.restore(snap));
    CHECK(cfg.get<int>("box.width") == 2);  // from the snapshot, not the disk
    REQUIRE(cfg.set("box.width", Node(3)));
    auto r = cfg.write_back("box.width");
    REQUIRE_FALSE(r);
    CHECK(
        r.error().message().ends_with("has changed since it was read; reload before writing back"));
    REQUIRE(cfg.reload());                  // the file differs from the restored state
    CHECK(cfg.get<int>("box.width") == 3);  // scratch still wins
}

TEST_CASE("taking a snapshot copies no array") {
    // Bookkeeping only (the state, the layer list and its strings, one scratch entry), so the
    // cost does not grow with the trees. Compared rather than bounded: debug STLs add their own.
    auto snapshot_allocations = [](int entries) {
        TempDir dir;
        std::string text;
        for (int i = 0; i < entries; ++i)
            text += "(k" + std::to_string(i) + " (v " + std::to_string(i) + "))\n";
        Config cfg({.tweaks = nullptr});
        REQUIRE(cfg.push_layer("big", dir.write("big.dta", text)));
        REQUIRE(cfg.set("k1.v", Node(5)));
        test::AllocationScope scope;
        const ConfigSnapshot snap = cfg.snapshot();
        const size_t allocations = scope.count();
        CHECK_FALSE(snap.empty());
        return allocations;
    };
    const size_t small = snapshot_allocations(2);
    const size_t big = snapshot_allocations(500);
    if (test::counting_allocations()) {
        CHECK(big == small);
        CHECK(small > 0);
    }
}
#endif
