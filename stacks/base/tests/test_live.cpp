#include "base/live.hpp"

#include <doctest.h>

#include "test_util.hpp"

static moira::Tweak<float> kValue{"live_test.value", -1.0f};

TEST_CASE("live: a saved edit is seen by the next frame, and timed from the save") {
    test::ScratchDir dir("live");
    REQUIRE(test::write(dir.path / "default.dta", "(live_test (value 1))"));
    moira::Context ctx;
    base::Live live(ctx);
    REQUIRE(live.open({.data_dir = dir.path.string()}));
    CHECK(float(kValue) == 1.0f);

    REQUIRE(test::write(dir.path / "default.dta", "(live_test (value 2))"));
    CHECK(test::within_two_seconds([&] {
        live.begin_frame();
        live.end_frame();  // the frame that shows it
        return float(kValue) == 2.0f;
    }));

    // The acceptance number (game-stacks.md §2.2): under a second from save to frame.
    const moira::LoopStats stats = live.config().loop_stats();
    bool found = false;
    for (const moira::LoopRow& row : stats.files) {
        if (row.edits == 0) continue;
        found = true;
        MESSAGE("edit to see: ", double(row.edit_median_ns) / 1e6, " ms");
        CHECK(row.edit_median_ns < 1'000'000'000u);
    }
    CHECK(found);
}

TEST_CASE("live: the layers, and a broken save keeps the old value") {
    test::ScratchDir dir("layers");
    REQUIRE(test::write(dir.path / "default.dta", "(live_test (value 1))"));
    REQUIRE(test::write(dir.path / "local.dta", "(live_test (value 5))"));
    moira::Context ctx;
    base::Live live(ctx);
    REQUIRE(live.open({.data_dir = dir.path.string(), .watch = false}));
    CHECK(float(kValue) == 5.0f);  // local over default
    auto source = live.config().source("live_test.value");
    REQUIRE(source);
    CHECK(source->layer.str() == "local");

    REQUIRE(test::write(dir.path / "agent.dta", "(live_test (value 7))"));
    CHECK(live.reload_now());
    CHECK(float(kValue) == 7.0f);  // agent over local

    REQUIRE(test::write(dir.path / "agent.dta", "(live_test (value 8)"));  // unclosed
    CHECK_FALSE(live.reload_now());
    CHECK(float(kValue) == 7.0f);
    std::vector<std::string> log;
    CHECK(live.take_log(log));
    REQUIRE(log.size() == 1);
    CHECK(log[0].find("agent.dta") != std::string::npos);
}

TEST_CASE("live: a missing default.dta fails to open, and says so") {
    test::ScratchDir dir("missing");
    moira::Context ctx;
    base::Live live(ctx);
    CHECK_FALSE(live.open({.data_dir = dir.path.string()}));
    std::vector<std::string> log;
    CHECK(live.take_log(log));
    CHECK(log.size() == 1);
    CHECK_FALSE(live.take_log(log));  // taken
}
