// M10: harness instrumentation (spec §4, §10; docs/decisions.md D90): loop statistics.
#include <doctest.h>
#include <moira/config.hpp>
#include <moira/std_interop.hpp>

#include "alloc_counter.hpp"
#include "temp_dir.hpp"

#include <string>
#include <vector>

using namespace moira;
using moira::test::TempDir;

namespace {

// Advances a fixed step per call. A step of 1 µs keeps every sample exact: the statistics
// keep microseconds.
struct FakeClock {
    uint64_t t = 0;
    uint64_t step = 1000;
    static uint64_t tick(void* self) {
        auto* c = static_cast<FakeClock*>(self);
        return c->t += c->step;
    }
};

[[maybe_unused]] const LoopRow* row(const List<LoopRow>& rows, std::string_view ends_with) {
    for (const LoopRow& r : rows)
        if (r.name.str().ends_with(ends_with)) return &r;
    return nullptr;
}

}  // namespace

#if !MOIRA_SHIPPING
TEST_CASE(
    "loop statistics: edit-to-see per file and per layer, to the reload's end, then to "
    "mark_visible") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("d.dta", "(speed 1)\n")));
    REQUIRE(cfg.push_layer("patch", dir.write("p.dta", "(jump 1)\n")));
    FakeClock clock;
    cfg.set_time_source(FakeClock::tick, &clock);

    // Every stage reads the clock, so a reload's times are whole microseconds.
    clock.t = 100000;
    dir.write("d.dta", "(speed 2)\n");
    REQUIRE(cfg.reload({.trigger = "watcher"_sym, .origin_ns = 50000}) == true);
    const auto rec = cfg.last_reload();
    REQUIRE(rec);
    CHECK(rec->layer == Symbol("default"));
    CHECK(rec->visible_ns == 0);
    const uint64_t first_edit = rec->end_ns - 50000, first_time = rec->end_ns - rec->start_ns;

    LoopStats s = cfg.loop_stats();
    CHECK(s.reloads == 1);
    REQUIRE(s.files.size() == 1);
    CHECK(s.files[0].name.str().ends_with("d.dta"));
    CHECK(s.files[0].reloads == 1);
    CHECK(s.files[0].edits == 1);
    CHECK(s.files[0].edit_median_ns == first_edit);  // to the reload's end: no frame yet
    CHECK(s.files[0].times == 1);
    CHECK(s.files[0].reload_median_ns == first_time);
    REQUIRE(s.layers.size() == 1);
    CHECK(s.layers[0].name == Symbol("default"));
    CHECK(s.layers[0].edit_median_ns == first_edit);

    // The frame that shows it, one tick later.
    cfg.mark_visible();
    CHECK(cfg.last_reload()->visible_ns == rec->end_ns + 1000);
    const uint64_t seen = first_edit + 1000;
    s = cfg.loop_stats();
    CHECK(s.files[0].edit_median_ns == seen);
    CHECK(s.files[0].edit_worst_ns == seen);
    CHECK(s.layers[0].edit_median_ns == seen);
    // Nothing waits now: the next frame reads no clock.
    const uint64_t before = clock.t;
    cfg.mark_visible();
    CHECK(clock.t == before);

    // A host polling a hundred times pushes nothing out.
    for (int i = 0; i < 100; ++i) REQUIRE(cfg.reload() == false);
    s = cfg.loop_stats();
    CHECK(s.reloads == 101);
    CHECK(s.unchanged == 100);
    CHECK(s.files[0].reloads == 1);
    CHECK(s.files[0].edits == 1);

    // Three more edits, saved 1, 5, and 3 µs before their reloads began: with the first,
    // four samples, of which the median is the upper middle one and the worst the first.
    int n = 3;
    std::vector<uint64_t> edits{seen};
    for (int k : {1, 5, 3}) {
        dir.write("d.dta", "(speed " + std::to_string(n++) + ")\n");
        const uint64_t origin = clock.t - uint64_t(k) * 1000;
        REQUIRE(cfg.reload({.origin_ns = origin}) == true);
        edits.push_back(cfg.last_reload()->end_ns - origin);
    }
    CHECK(edits[2] > edits[3]);  // the samples differ as their origins do
    CHECK(edits[3] > edits[1]);
    s = cfg.loop_stats();
    CHECK(s.files[0].reloads == 4);
    CHECK(s.files[0].edits == 4);
    CHECK(s.files[0].edit_median_ns == edits[2]);  // sorted: edits[1], edits[3], edits[2], seen
    CHECK(s.files[0].edit_worst_ns == seen);
    // A reload without an origin counts, and has a reload time, but no edit-to-see.
    dir.write("d.dta", "(speed 9)\n");
    REQUIRE(cfg.reload() == true);
    s = cfg.loop_stats();
    CHECK(s.files[0].reloads == 5);
    CHECK(s.files[0].edits == 4);
    CHECK(s.files[0].times == 5);

    // A failed reload counts against its file, its layer, and the total.
    dir.write("p.dta", "(jump\n");
    CHECK_FALSE(cfg.reload({.origin_ns = clock.t}));
    s = cfg.loop_stats();
    CHECK(s.failures == 1);
    REQUIRE(s.files.size() == 2);
    CHECK(s.files[0].name.str().ends_with("p.dta"));  // most recent first
    CHECK(s.files[0].failures == 1);
    CHECK(s.files[0].edits == 0);
    CHECK(s.files[0].times == 0);
    const LoopRow* patch = row(s.layers, "patch");
    REQUIRE(patch);
    CHECK(patch->failures == 1);
    CHECK(cfg.last_reload()->outcome == ReloadOutcome::ParseError);
}

TEST_CASE("loop statistics keep the 32 most recently changed files") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    std::string main;
    for (int i = 0; i < 33; ++i) {
        dir.write("f" + std::to_string(i) + ".dta", "(f" + std::to_string(i) + " 0)\n");
        main += "#include f" + std::to_string(i) + ".dta\n";
    }
    REQUIRE(cfg.push_layer("default", dir.write("main.dta", main)));
    for (int i = 0; i < 33; ++i) {
        dir.write("f" + std::to_string(i) + ".dta", "(f" + std::to_string(i) + " 1)\n");
        REQUIRE(cfg.reload() == true);
    }
    const LoopStats s = cfg.loop_stats();
    CHECK(s.files.size() == 32);
    CHECK(s.files[0].name.str().ends_with("f32.dta"));
    CHECK_FALSE(row(s.files, "/f0.dta"));  // the least recently changed gave up its row
    CHECK(row(s.files, "/f1.dta"));
    REQUIRE(s.layers.size() == 1);
    CHECK(s.layers[0].reloads == 33);
}

TEST_CASE("mark_visible and a reload that feeds the statistics allocate nothing") {
    if (!test::counting_allocations()) return;
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("d.dta", "(speed 1)\n")));
    dir.write("d.dta", "(speed 2)\n");
    REQUIRE(cfg.reload({.origin_ns = 1}));  // makes the history and the table
    test::AllocationScope scope;
    cfg.mark_visible();
    for (int i = 0; i < 10; ++i) cfg.mark_visible();
    CHECK(scope.count() == 0);
}
#endif

#if MOIRA_SHIPPING
TEST_CASE("under MOIRA_SHIPPING loop statistics are empty and mark_visible does nothing") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("d", dir.write("d.dta", "(a 1)\n")));
    cfg.mark_visible();
    const LoopStats s = cfg.loop_stats();
    CHECK(s.reloads == 0);
    CHECK(s.files.empty());
    CHECK(s.layers.empty());
}
#endif
