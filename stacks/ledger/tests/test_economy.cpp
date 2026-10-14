#include "economy.hpp"

#include <moira/parse.hpp>

#include <doctest.h>

#include <cmath>
#include <string>

namespace {

const char* kData = R"(
(goods
   (wheat (base_price 2) (demand 100))
   (flour (base_price 7) (demand 50) (spoil_days 10)))
(buildings
   (farm (cost 400) (upkeep 5) (rate 20) (recipe -> wheat))
   (mill (cost 1000) (upkeep 10) (rate 5) (recipe wheat wheat -> flour)
         (unlock {>= {econ.total_earned} 100})))
(strategies (greedy (horizon 60)) (random (build_chance 0.5)))
(milestones (first_mill (desc "A mill") (when {>= {econ.count mill} 1}) (target_day 30)))
)";

ledger::Economy economy(const char* text = kData) {
    auto tree = moira::read_string(text);
    REQUIRE(tree);
    ledger::Economy e;
    std::string error;
    REQUIRE_MESSAGE(ledger::load(*tree, e, error), error);
    return e;
}

}  // namespace

TEST_CASE("ledger: recipes read as inputs, an arrow, an output") {
    const ledger::Economy e = economy();
    REQUIRE(e.buildings.size() == 2);
    CHECK(e.buildings[0].inputs.empty());
    CHECK(e.buildings[0].output == e.good("wheat"));
    CHECK(e.buildings[1].inputs.size() == 2);
    CHECK(e.buildings[1].output == e.good("flour"));
    CHECK(e.buildings[1].unlock.type() == moira::Type::Command);
    CHECK(e.strategies[0].kind == "greedy");
    CHECK(e.milestones[0].target_day == 30);

    std::string error;
    ledger::Economy bad;
    auto tree = moira::read_string("(goods (a (base_price 1))) (buildings (x (recipe a -> b)))");
    REQUIRE(tree);
    CHECK_FALSE(ledger::load(*tree, bad, error));
    CHECK(error.find("b") != std::string::npos);
}

TEST_CASE("ledger: selling fills the market, and the market recovers") {
    const ledger::Economy e = economy();
    ledger::Rules rules;
    ledger::Company c;
    ledger::start(c, e, rules, "p", -1, 1);
    const int wheat = e.good("wheat");
    CHECK(c.price(e, wheat) == doctest::Approx(2.0));
    c.stock[size_t(wheat)] = 100;
    const int64_t coins = ledger::sell(c, e, wheat, 100);
    // The integral of 2 * 100 / (100 + s) over s in 0..100: 200 ln 2.
    CHECK(coins == std::llround(200 * std::log(2.0)));
    CHECK(c.price(e, wheat) == doctest::Approx(1.0));
    c.sell.assign(e.goods.size(), false);
    ledger::day(c, e, rules);
    CHECK(c.price(e, wheat) > 1.0);
}

TEST_CASE("ledger: a day produces, consumes inputs, pays upkeep") {
    const ledger::Economy e = economy();
    ledger::Rules rules;
    ledger::Company c;
    ledger::start(c, e, rules, "p", -1, 1);
    CHECK_FALSE(ledger::build(c, e, e.building("mill")));  // locked
    REQUIRE(ledger::build(c, e, e.building("farm")));
    CHECK(c.money == 600);
    c.unlocked[size_t(e.building("mill"))] = true;
    c.money += 1000;
    REQUIRE(ledger::build(c, e, e.building("mill")));
    c.sell.assign(e.goods.size(), false);
    ledger::day(c, e, rules);
    // Buildings run in order: the farm's 20 wheat feed the mill's 5 batches the same day.
    CHECK(c.stock[size_t(e.good("wheat"))] == 10);
    CHECK(c.stock[size_t(e.good("flour"))] == 5);
    CHECK(c.money == 600 - 15);
    ledger::day(c, e, rules);
    CHECK(c.stock[size_t(e.good("wheat"))] == 20);
    CHECK(c.stock[size_t(e.good("flour"))] == 9);  // 10, less a tenth spoiled
}

TEST_CASE("ledger: the same seed gives the same run; strategies share the market's days") {
    const ledger::Economy e = economy();
    ledger::Rules rules;
    auto run = [&](int strategy, uint64_t seed) {
        ledger::Company c;
        ledger::start(c, e, rules, "bot", strategy, seed);
        c.unlocked.assign(e.buildings.size(), true);
        for (int d = 0; d < 400; ++d) ledger::day(c, e, rules);
        return std::to_string(c.money) + ":" + std::to_string(c.owned[0]) + ":" +
               std::to_string(c.owned[1]);
    };
    CHECK(run(0, 3) == run(0, 3));
    CHECK(run(1, 3) != run(1, 4));
    ledger::Company greedy;
    ledger::start(greedy, e, rules, "g", 0, 9);
    for (int d = 0; d < 200; ++d) ledger::day(greedy, e, rules);
    CHECK(greedy.owned[0] > 1);  // it found the farm worth building, more than once
}
