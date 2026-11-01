// Baked tables (table.hpp, D292): a viewed keyed shape's records as a packed array, built
// from the merged tree, read by index, key, path, ID and parent, rebuilt by identity. The
// views are tests/shape/views/, the golden headers.
#include <doctest.h>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/table.hpp>

#include "alloc_counter.hpp"
#include "item_read.hpp"
#include "move_read.hpp"
#include "ok.hpp"

#include <string>
#include <vector>

using namespace baker;
using baker::test::ok;
using views_test::Item;
using views_test::Move;

namespace {

const char* const kFighters =
    "(fighters\n"
    "  (kestrel (moves\n"
    "    (jab (@id a0000000000000000000000000000001) (startup 4) (damage 3.5) (name \"Jab\")"
    " (button light) (armored 1) (cancel_into heavy kick) (hitbox (x 1) (y 2.5))"
    " (points (p (x 0) (y 0)) (p (x 3) (y 1))) (ramp (0 0) (1 1)) (on_hit {do_hit}))\n"
    "    (heavy (@aka slam) (startup 9) (damage 12))\n"
    "    (kick (startup 7) (damage 8))))\n"
    "  (wren (moves\n"
    "    (poke (@id a0000000000000000000000000000002) (startup 3) (damage 2)))))\n"
    "(items (potion (price 12.5) (tag \"heals\") (sizes 1 2 3)) (sword (price 150)))\n";

std::string text(const Error& e) {
    return std::string(to_string(e));
}

}  // namespace

TEST_CASE("a table holds the records of a keyed shape, packed, with their strings and lists") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(add_table<Move>(cfg));
    REQUIRE(add_table<Item>(cfg));
    REQUIRE(cfg.push_layer_text("base", kFighters));
    const Table<Move> moves = table<Move>(cfg);
    REQUIRE(moves);
    REQUIRE(moves.size() == 4);
    const Move& jab = moves[0];
    CHECK(jab.startup == 4);
    CHECK(jab.damage == 3.5f);
    CHECK(jab.name == "Jab");
    CHECK(jab.button == Symbol("light"));
    CHECK(jab.armored);
    // Lists are packed: the elements are the table's, converted once.
    CHECK(jab.cancel_into.is_packed());
    REQUIRE(jab.cancel_into.size() == 2);
    CHECK(jab.cancel_into[1] == Symbol("kick"));
    CHECK(jab.cancel_into.elements()[0] == Symbol("heavy"));
    CHECK(jab.hitbox.y == 2.5f);
    REQUIRE(jab.points.size() == 2);
    CHECK(jab.points[1].x == 3.0f);
    CHECK(jab.points.elements()[1].y == 1.0f);
    CHECK(jab.ramp.size() == 2);
    CHECK(jab.ramp.sample(0.5) == doctest::Approx(0.5));
    // The strings are the table's, not the tree's.
    const Ref<Array> root = cfg.root();
    const Array* tree_jab = root->try_get("fighters.kestrel.moves.jab");
    REQUIRE(tree_jab);
    const std::string_view tree_name = *tree_jab->find_str(Symbol("name"));
    CHECK(tree_name.data() != jab.name.data());
    // Rows are in tree order, grouped by parent; keys and IDs ride along.
    CHECK(moves.key_of(1) == Symbol("heavy"));
    CHECK(moves.key_of(3) == Symbol("poke"));
    CHECK(moves.id_of(0) == *RecordId::parse("a0000000000000000000000000000001"));
    CHECK(moves.id_of(1).empty());
    CHECK(moves[3].startup == 3);

    const Table<Item> items = table<Item>(cfg);
    REQUIRE(items.size() == 2);
    CHECK(items[0].price == 12.5f);
    CHECK(items[0].tag == "heals");
    REQUIRE(items[0].sizes.size() == 3);
    CHECK(items[0].sizes[2] == 3);
    CHECK(items[1].tag.empty());
    CHECK(items[1].sizes.empty());
}

TEST_CASE("a table is read by key, by path, by ID, by alias and by parent") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(add_table<Move>(cfg));
    REQUIRE(add_table<Item>(cfg));
    REQUIRE(cfg.push_layer_text("base", kFighters));
    const Table<Move> moves = table<Move>(cfg);
    const Table<Item> items = table<Item>(cfg);
    // A shape with no `*`: by key.
    CHECK(items.index_of(Symbol("sword")) == 1);
    CHECK_FALSE(items.index_of(Symbol("potion.x")));
    CHECK(items.index_of("potion") == 0);
    // One with `*`: by the path below the table.
    CHECK(moves.index_of("kestrel.jab") == 0);
    CHECK(moves.index_of("kestrel.kick") == 2);
    CHECK(moves.index_of("wren.poke") == 3);
    CHECK(moves.index_of(Path("kestrel.heavy")) == 1);
    CHECK_FALSE(moves.index_of("kestrel.poke"));
    CHECK_FALSE(moves.index_of("jab"));  // a key alone is not a path below this table
    CHECK_FALSE(moves.index_of(Symbol("jab")));
    CHECK_FALSE(moves.index_of("kestrel.moves.jab"));
    CHECK_FALSE(moves.index_of("kestrel.jab.startup"));
    // By ID, and by an alias (a renamed record's old key).
    CHECK(moves.index_of(*RecordId::parse("a0000000000000000000000000000002")) == 3);
    CHECK_FALSE(moves.index_of(RecordId{}));
    CHECK_FALSE(moves.index_of(*RecordId::parse("a0000000000000000000000000000009")));
    CHECK(moves.index_of("kestrel.slam") == 1);
    CHECK_FALSE(moves.index_of("wren.slam"));
    // By parent: a contiguous range.
    const IndexRange k = moves.children_of(Symbol("kestrel"));
    CHECK(k.first == 0);
    CHECK(k.count == 3);
    int n = 0;
    for (const uint32_t i : k) n += moves[i].startup;
    CHECK(n == 20);
    CHECK(moves.children_of(Symbol("wren")).first == 3);
    CHECK(moves.children_of(Symbol("nobody")).empty());
    CHECK(items.children_of(Symbol("potion")).empty());
    CHECK(moves.block()->wildcards() == 1);
    CHECK(items.block()->wildcards() == 0);
    // at() checks, operator[] does not.
    CHECK(moves.at(4) == nullptr);
    CHECK(moves.at(3)->startup == 3);
}

TEST_CASE("a table is rebuilt when its records change, a held handle keeps the old one") {
    Config cfg({.tweaks = nullptr});
    REQUIRE(add_table<Move>(cfg));
    REQUIRE(add_table<Item>(cfg));
    REQUIRE(cfg.push_layer_text("base", kFighters));
    const Table<Move> before = table<Move>(cfg);
    const Table<Item> items_before = table<Item>(cfg);
    // A layer that retunes a move: the moves table is rebuilt, the items table is shared.
    REQUIRE(cfg.push_layer_text("tune", "(fighters (kestrel (moves (jab (damage 4)))))"));
    const Table<Move> after = table<Move>(cfg);
    CHECK(after.block() != before.block());
    CHECK(table<Item>(cfg).block() == items_before.block());
    CHECK(after[0].damage == 4.0f);
    CHECK(before[0].damage == 3.5f);  // the held handle reads what it held
    // Indices are stable while the set of records is unchanged.
    CHECK(after.index_of("kestrel.kick") == 2);
    CHECK(after.index_of("wren.poke") == 3);
    // A record added renumbers what follows it; one removed too.
    REQUIRE(cfg.push_layer_text("more",
                                "(fighters (kestrel (moves (throw (startup 11) (damage 1)))))"));
    const Table<Move> more = table<Move>(cfg);
    CHECK(more.size() == 5);
    CHECK(more.index_of("kestrel.throw") == 3);
    CHECK(more.index_of("wren.poke") == 4);
    REQUIRE(cfg.remove_layer("more"));
    CHECK(table<Move>(cfg).size() == 4);
    // A live edit reaches the table at apply_edits.
    if constexpr (!kShipping) {
        REQUIRE(cfg.set("fighters.wren.moves.poke.startup", 5));
        CHECK(table<Move>(cfg)[3].startup == 3);
        cfg.apply_edits();
        CHECK(table<Move>(cfg)[3].startup == 5);
    }
    // Registered after the load: built at once.
    Config late({.tweaks = nullptr});
    REQUIRE(late.push_layer_text("base", kFighters));
    CHECK_FALSE(table<Move>(late));
    REQUIRE(add_table<Move>(late));
    CHECK(table<Move>(late).size() == 4);
    auto twice = add_table<Move>(late);
    REQUIRE_FALSE(twice);
    CHECK(text(twice.error()).find("registered already") != std::string::npos);
}

TEST_CASE("a field that is not a literal reads as its default, with a located error") {
    Config cfg({.tweaks = nullptr});
    std::vector<std::string> warnings;
    cfg.set_warning_handler([&](const Error& e) { warnings.push_back(text(e)); });
    REQUIRE(add_table<Move>(cfg));
    REQUIRE(cfg.push_layer_text("base",
                                "(fighters (kestrel (moves (jab (startup 4) (damage {compute}))"
                                " (kick (startup \"soon\") (damage 1)))))"));
    const Table<Move> moves = table<Move>(cfg);
    REQUIRE(moves.size() == 2);
    CHECK(moves[0].startup == 4);
    CHECK(moves[0].damage == 0.0f);
    CHECK(moves[1].damage == 1.0f);  // the reader goes on past the failed field
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].find("damage") != std::string::npos);
    CHECK(warnings[0].find("fighters.kestrel.moves.jab") != std::string::npos);
    CHECK(warnings[1].find("startup") != std::string::npos);
}

TEST_CASE("reading a table allocates nothing") {
    // Building one allocates through the Config's allocator alone: baker_alloc_tests.
    if (!test::counting_allocations()) return;
    Config cfg({.tweaks = nullptr});
    REQUIRE(add_table<Move>(cfg));
    REQUIRE(cfg.push_layer_text("base", kFighters));
    const Table<Move> moves = table<Move>(cfg);
    size_t allocations = 0;
    Symbol first;
    {
        const test::AllocationScope scope;
        const auto i = moves.index_of("kestrel.jab");
        const auto by_id = moves.index_of(*RecordId::parse("a0000000000000000000000000000001"));
        const Move& jab = moves[i.value_or(9)];
        first = jab.cancel_into[0];
        allocations = scope.count() + (by_id == i ? 0 : 100);
    }
    CHECK(first == Symbol("heavy"));
    CHECK(allocations == 0);
}
