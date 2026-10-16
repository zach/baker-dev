#include "base/watcher.hpp"

#include <doctest.h>

#include <chrono>
#include <cstdint>

#include "test_util.hpp"

namespace fs = std::filesystem;

TEST_CASE("watcher: a saved file is reported, with its write time") {
    test::ScratchDir dir("watch");
    REQUIRE(test::write(dir.path / "default.dta", "(a 1)"));
    base::Watcher w;
    REQUIRE(w.watch(dir.path));
    base::Changes c;
    CHECK_FALSE(w.poll(c));

    REQUIRE(test::write(dir.path / "default.dta", "(a 2)"));
    CHECK(test::within_two_seconds([&] { return w.poll(c); }));
    REQUIRE(!c.files.empty());
    CHECK(fs::path(c.files.back()).filename() == "default.dta");
    std::error_code ec;
    CHECK(c.newest_write_ns ==
          base::file_clock_ns(fs::last_write_time(dir.path / "default.dta", ec)));
    CHECK(c.newest_write_ns <= base::file_clock_now_ns());
}

TEST_CASE("watcher: file times are nanoseconds since 1970, which script integers hold") {
    // Live hands this clock to the Config, and the journal and loop records carry it into
    // script as 64-bit signed integers: a time at or above 2^63 would read as negative.
    const uint64_t now = base::file_clock_now_ns();
    const auto sys = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    CHECK(now < (uint64_t{1} << 63));
    const int64_t apart = static_cast<int64_t>(now) - sys;
    CHECK(apart > -60'000'000'000);  // the same clock, read a moment apart
    CHECK(apart < 60'000'000'000);
}

TEST_CASE("watcher: other extensions are ignored") {
    test::ScratchDir dir("ignore");
    base::Watcher w;
    REQUIRE(w.watch(dir.path));
    REQUIRE(test::write(dir.path / "default.dta.swp", "x"));
    REQUIRE(test::write(dir.path / "notes.txt", "x"));
    REQUIRE(test::write(dir.path / "marker.dta", "(m)"));
    // The .dta written last arrives after the others: once it shows, they would have.
    base::Changes c;
    REQUIRE(test::within_two_seconds([&] { return w.poll(c); }));
    for (const std::string& f : c.files) CHECK(fs::path(f).extension() == ".dta");
}

TEST_CASE("watcher: subdirectories, including ones made after watching began") {
    test::ScratchDir dir("tree");
    fs::create_directories(dir.path / "packs");
    base::Watcher w;
    REQUIRE(w.watch(dir.path));

    base::Changes c;
    REQUIRE(test::write(dir.path / "packs" / "game.dta", "(pack game (level game))"));
    CHECK(test::within_two_seconds([&] { return w.poll(c); }));

    fs::create_directories(dir.path / "levels");
    (void)w.poll(c);  // the new directory's event adds its watch
    REQUIRE(test::write(dir.path / "levels" / "one.dta", "(level one)"));
    CHECK(test::within_two_seconds([&] { return w.poll(c); }));
}

TEST_CASE("watcher: a directory that does not exist is an error") {
    base::Watcher w;
    CHECK_FALSE(w.watch("this/does/not/exist"));
    CHECK_FALSE(w.error().empty());
}
