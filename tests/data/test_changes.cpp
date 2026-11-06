// The change log (D298): transactions of change records beside the journal, opt-in; step
// 1 records live edits.
#include <doctest.h>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include "alloc_counter.hpp"
#include "ok.hpp"

#include <string>
#include <vector>

using namespace baker;
using baker::test::ok;

namespace {

std::string text(const Array& a) {
    return to_string(a);
}

std::vector<Transaction> all(const Config& cfg) {
    std::vector<Transaction> out;
    cfg.transactions([&](const Transaction& t) { out.push_back(t); });
    return out;
}

}  // namespace

TEST_CASE("the change log is off until asked for, and then records each live edit whole") {
    if constexpr (kShipping) return;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer_text("base", "(player (speed 3.5) (tint 1 2 3 4 5 6))"));
    REQUIRE(cfg.set("player.speed", Node(4.0)));
    CHECK(cfg.transaction_count() == 0);
    CHECK(cfg.change_history() == 0);

    cfg.set_change_history(16);
    REQUIRE(cfg.set("player.speed", Node(5.0)));  // the fast path: speed is in scratch
    REQUIRE(cfg.set(Path("player.tint"), std::vector<Node>{Node(9), Node(8), Node(7), Node(6),
                                                           Node(5), Node(4), Node(3)}));
    REQUIRE(cfg.set("player.lives", Node(int64_t{3})));  // a path the stack lacks
    const auto ts = all(cfg);
    REQUIRE(ts.size() == 3);
    CHECK(ts[0].id == 1);
    CHECK(ts[0].name == Symbol("set"));
    REQUIRE(ts[0].records.size() == 1);
    const ChangeRecord& speed = ts[0].records[0];
    CHECK(speed.kind == ChangeKind::Set);
    CHECK(speed.path == Symbol("player.speed"));
    CHECK(text(*speed.before) == "(4.0)");
    CHECK(text(*speed.after) == "(5.0)");
    CHECK(speed.layer == Symbol("scratch"));
    CHECK(speed.source.layer == Symbol("scratch"));  // the old value was scratch's own
    // The whole value, past the journal's four nodes.
    const ChangeRecord& tint = ts[1].records[0];
    CHECK(text(*tint.before) == "(1 2 3 4 5 6)");
    CHECK(text(*tint.after) == "(9 8 7 6 5 4 3)");
    CHECK(tint.source.layer == Symbol("base"));
    // A value the stack lacked: no before.
    const ChangeRecord& lives = ts[2].records[0];
    CHECK_FALSE(lives.before);
    CHECK(text(*lives.after) == "(3)");
    CHECK(ts[2].time_ns >= ts[0].time_ns);
    CHECK(ts[2].id == 3);
}

TEST_CASE("a transaction groups the changes until its end, and nests") {
    if constexpr (kShipping) return;
    Config cfg({.tweaks = nullptr});
    cfg.set_change_history(8);
    REQUIRE(cfg.push_layer_text("base", "(a 1) (b 2) (c 3)"));
    cfg.begin_transaction("drag");
    REQUIRE(cfg.set("a", Node(10)));
    cfg.begin_transaction("inner");
    REQUIRE(cfg.set("b", Node(20)));
    cfg.end_transaction();
    REQUIRE(cfg.set("c", Node(30)));
    CHECK(cfg.transaction_count() == 0);  // still open
    cfg.end_transaction();
    const auto ts = all(cfg);
    REQUIRE(ts.size() == 1);
    CHECK(ts[0].name == Symbol("drag"));
    REQUIRE(ts[0].records.size() == 3);
    CHECK(ts[0].records[1].path == Symbol("b"));
    // An empty transaction is not kept; an unbalanced end is harmless.
    cfg.begin_transaction("nothing");
    cfg.end_transaction();
    cfg.end_transaction();
    CHECK(cfg.transaction_count() == 1);
}

TEST_CASE("the log is a ring of the last transactions; clear_scratch records what it drops") {
    if constexpr (kShipping) return;
    Config cfg({.tweaks = nullptr});
    cfg.set_change_history(3);
    REQUIRE(cfg.push_layer_text("base", "(a 1)"));
    for (int i = 0; i < 5; ++i) REQUIRE(cfg.set("a", Node(int64_t{i + 2})));
    auto ts = all(cfg);
    REQUIRE(ts.size() == 3);
    CHECK(ts[0].id == 3);
    CHECK(text(*ts[0].records[0].after) == "(4)");
    CHECK(ts[2].id == 5);
    CHECK(text(*ts[2].records[0].after) == "(6)");
    // clear_scratch: the edit's value gives way to the file's.
    REQUIRE(cfg.clear_scratch("a"));
    ts = all(cfg);
    REQUIRE(ts.size() == 3);
    const Transaction& cleared = ts[2];
    CHECK(cleared.name == Symbol("clear_scratch"));
    REQUIRE(cleared.records.size() == 1);
    CHECK(text(*cleared.records[0].before) == "(6)");
    CHECK(text(*cleared.records[0].after) == "(1)");
    CHECK(cleared.records[0].source.layer == Symbol("scratch"));
    CHECK(cleared.records[0].layer == Symbol("base"));
    CHECK(ok(cfg.get<int>("a")) == 1);
    // Turning the log off clears it; set_change_history(0) keeps the fast path clean.
    cfg.set_change_history(0);
    CHECK(cfg.transaction_count() == 0);
    REQUIRE(cfg.set("a", Node(int64_t{7})));
    CHECK(cfg.transaction_count() == 0);
}

TEST_CASE("with the log off a scratch set allocates nothing; with it on, it keeps its trees") {
    if constexpr (kShipping) return;
    if (!test::counting_allocations()) return;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer_text("base", "(a 1)"));
    REQUIRE(cfg.set("a", Node(2)));
    size_t off = 0;
    {
        const test::AllocationScope scope;
        for (int i = 0; i < 100; ++i) (void)cfg.set("a", Node(i));
        off = scope.count();
    }
    CHECK(off == 0);
    cfg.set_change_history(8);
    for (int i = 0; i < 10; ++i) (void)cfg.set("a", Node(i));
    CHECK(cfg.transaction_count() == 8);  // the allocation gate counts the log's allocator
}
