// Ledger's economy (game-stacks.md §4.2): goods, buildings that turn goods into goods, a
// market per company whose prices fall as it sells, and the bot strategies the simulation
// plays it with. One tick is one in-game day. No window and no script here: the game
// evaluates the data's commands (unlocks, milestones) and calls in.
//
// Money is an int64 count of coins (Baker's integers are 64-bit, so late-game balances fit
// in data and script); prices are doubles. A run is a function of the data and the seed.
#pragma once

#include <baker/array.hpp>
#include <base/rng.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ledger {

struct Good {
    std::string name;
    double base_price = 1;
    double demand = 100;    // units a day the market takes at about the base price
    int spoil_days = 0;     // 0: keeps; else a 1/spoil_days share of the stock spoils daily
    double volatility = 0;  // the day's demand varies by up to this share, either way
};

struct Building {
    std::string name;
    int64_t cost = 0;
    double upkeep = 0;        // coins a day
    std::vector<int> inputs;  // goods consumed per batch, one entry per unit
    int output = -1;          // the good one batch makes (one unit)
    int rate = 1;             // batches a day
    baker::Node unlock;       // a command, or a number (0: locked)
    bool has_unlock = false;  // false: unlocked from the start
};

struct Strategy {
    std::string name;
    std::string kind;         // greedy, hoarder, specialist, random
    double reserve = 0;       // keeps this many times a building's cost before buying it
    double sell_above = 0;    // hoarder: sells a good only above this share of base price
    double build_chance = 1;  // random: chance a day of building something
    int horizon = 60;         // greedy: builds only what pays back within this many days
    std::string chain;        // specialist: the good it builds toward
};

struct Milestone {
    std::string name, desc;
    baker::Node when;  // a command; true once reached
    int target_day = 0;
};

struct Rules {
    int64_t start_money = 1000;
    double recovery = 0.25;  // the share of market saturation that clears each day
    double resale = 0.5;     // a building's worth, as a share of its cost
    int bankrupt_days = 30;  // days in debt before a company is bankrupt
};

struct Economy {
    std::vector<Good> goods;
    std::vector<Building> buildings;
    std::vector<Strategy> strategies;
    std::vector<Milestone> milestones;
    baker::Ref<baker::Array> tree;  // keeps the commands alive

    [[nodiscard]] int good(const std::string& name) const;
    [[nodiscard]] int building(const std::string& name) const;
    [[nodiscard]] int strategy(const std::string& name) const;
};

// Reads (goods ...), (buildings ...), (strategies ...), and (milestones ...) from the merged
// data. False, with the reason, on a recipe naming no good or a malformed recipe.
[[nodiscard]] bool load(const baker::Ref<baker::Array>& root, Economy& out, std::string& error);

struct Company {
    std::string name;
    int strategy = -1;  // -1: a person plays it
    int64_t money = 0;
    int64_t earned = 0, spent = 0;  // all sales; all purchases and upkeep
    std::vector<int64_t> stock, made;
    std::vector<int> owned;
    std::vector<int> ever_owned;  // most of each building held at once
    std::vector<bool> unlocked, sell;
    std::vector<double> saturation;     // the company's market, per good: units unabsorbed
    std::vector<double> demand_factor;  // today's demand, as a share of the good's
    std::vector<int> milestone_day;     // -1: not yet
    int day = 0, debt_days = 0;
    bool bankrupt = false;
    std::vector<int64_t> worth_by_day;  // net worth at the end of each day
    base::Rng rng, market_rng;

    [[nodiscard]] double price(const Economy& e, int good) const;
    [[nodiscard]] int64_t worth(const Economy& e, const Rules& r) const;
};

void start(Company& c, const Economy& e, const Rules& r, const std::string& name, int strategy,
           uint64_t seed);
// Buys one building; false if it is locked, unaffordable, or the company is bankrupt.
bool build(Company& c, const Economy& e, int building);
// Sells one building back at its resale value; false if none is owned.
bool demolish(Company& c, const Economy& e, const Rules& r, int building);
// Sells `units` of a good (at most the stock); returns the coins received.
int64_t sell(Company& c, const Economy& e, int good, int64_t units);
// One day: production, spoilage, the strategy's choices (or the player's standing sell
// orders), sales, upkeep, the market's recovery, and bankruptcy. A strategy other than
// random, in debt, sells its least profitable building. Unlocks and milestones are
// the caller's: they are commands in the data.
void day(Company& c, const Economy& e, const Rules& r);
// Revenue a building makes a day at today's prices, less its inputs and upkeep.
[[nodiscard]] double daily_profit(const Company& c, const Economy& e, int building);

}  // namespace ledger
