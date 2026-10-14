#include "economy.hpp"

#include <baker/std_interop.hpp>
#include <base/rng.hpp>
#include <base/words.hpp>

#include <algorithm>
#include <cmath>

namespace ledger {
namespace {

double get_float(const baker::Array& a, const char* tag, double fallback) {
    return a.find_float(baker::Symbol(tag), fallback).value_or(fallback);
}

std::string get_sym(const baker::Array& a, const char* tag) {
    auto s = a.find_sym(baker::Symbol(tag), baker::Symbol());
    return s ? std::string(s->str()) : std::string();
}

std::string get_text(const baker::Array& a, const char* tag) {
    if (const baker::Array* f = a.try_find(baker::Symbol(tag)); f && f->size() > 1) {
        if (auto s = (*f)[1].as_str()) return std::string(*s);
        if (auto s = (*f)[1].as_sym()) return std::string(s->str());
    }
    return {};
}

// The second value of (tag value), raw: a command stays a command.
baker::Node raw(const baker::Array& a, const char* tag) {
    if (const baker::Array* f = a.try_find(baker::Symbol(tag)); f && f->size() > 1) return (*f)[1];
    return {};
}

// Today's demand for a good: its base demand, varied by the day's draw.
double demand_today(const Company& c, const Good& g, int good) {
    return g.demand * c.demand_factor[size_t(good)];
}

}  // namespace

int Economy::good(const std::string& name) const {
    for (size_t i = 0; i < goods.size(); ++i)
        if (goods[i].name == name) return int(i);
    return -1;
}
int Economy::building(const std::string& name) const {
    for (size_t i = 0; i < buildings.size(); ++i)
        if (buildings[i].name == name) return int(i);
    return -1;
}
int Economy::strategy(const std::string& name) const {
    for (size_t i = 0; i < strategies.size(); ++i)
        if (strategies[i].name == name) return int(i);
    return -1;
}

bool load(const baker::Ref<baker::Array>& root, Economy& out, std::string& error) {
    Economy e;
    e.tree = root;
    if (const baker::Array* goods = root->try_find(baker::Symbol("goods")))
        for (const baker::Array& a : goods->subarrays()) {
            Good g;
            g.name = base::name(a);
            g.base_price = get_float(a, "base_price", 1);
            g.demand = std::max(1.0, get_float(a, "demand", 100));
            g.spoil_days = static_cast<int>(get_float(a, "spoil_days", 0));
            g.volatility = std::clamp(get_float(a, "volatility", 0), 0.0, 0.9);
            e.goods.push_back(std::move(g));
        }
    if (e.goods.empty()) {
        error = "no (goods ...) in the data";
        return false;
    }
    if (const baker::Array* buildings = root->try_find(baker::Symbol("buildings")))
        for (const baker::Array& a : buildings->subarrays()) {
            Building b;
            b.name = base::name(a);
            b.cost = static_cast<int64_t>(std::llround(get_float(a, "cost", 0)));
            b.upkeep = get_float(a, "upkeep", 0);
            b.rate = std::max(1, static_cast<int>(get_float(a, "rate", 1)));
            b.unlock = raw(a, "unlock");
            b.has_unlock = a.try_find(baker::Symbol("unlock")) != nullptr;
            // (recipe wheat wheat -> flour): inputs, an arrow, one output.
            const baker::Array* recipe = a.try_find(baker::Symbol("recipe"));
            bool arrow = false;
            for (size_t i = 1; recipe && i < recipe->size(); ++i) {
                auto s = (*recipe)[i].as_sym();
                const std::string name = s ? std::string(s->str()) : std::string();
                if (name == "->") {
                    arrow = true;
                    continue;
                }
                const int g = e.good(name);
                if (g < 0) {
                    error = "building " + b.name + ": the recipe names " +
                            (name.empty() ? std::string("something") : name) +
                            ", which is not one of (goods ...)";
                    return false;
                }
                if (arrow) {
                    if (b.output >= 0) {
                        error = "building " + b.name + ": a recipe makes one good";
                        return false;
                    }
                    b.output = g;
                } else {
                    b.inputs.push_back(g);
                }
            }
            if (b.output < 0) {
                error = "building " + b.name + " needs (recipe inputs... -> output)";
                return false;
            }
            e.buildings.push_back(std::move(b));
        }
    if (const baker::Array* list = root->try_find(baker::Symbol("strategies")))
        for (const baker::Array& a : list->subarrays()) {
            Strategy s;
            s.name = base::name(a);
            s.kind = get_sym(a, "kind");
            if (s.kind.empty()) s.kind = s.name;
            s.reserve = get_float(a, "reserve", 0);
            s.sell_above = get_float(a, "sell_above", 0);
            s.build_chance = get_float(a, "build_chance", 1);
            s.horizon = static_cast<int>(get_float(a, "horizon", 60));
            s.chain = get_sym(a, "chain");
            e.strategies.push_back(std::move(s));
        }
    if (const baker::Array* list = root->try_find(baker::Symbol("milestones")))
        for (const baker::Array& a : list->subarrays()) {
            Milestone m;
            m.name = base::name(a);
            m.desc = get_text(a, "desc");
            m.when = raw(a, "when");
            m.target_day = static_cast<int>(get_float(a, "target_day", 0));
            e.milestones.push_back(std::move(m));
        }
    out = std::move(e);
    return true;
}

double Company::price(const Economy& e, int good) const {
    const Good& g = e.goods[size_t(good)];
    const double d = demand_today(*this, g, good);
    return g.base_price * d / (d + saturation[size_t(good)]);
}

int64_t Company::worth(const Economy& e, const Rules& r) const {
    double w = static_cast<double>(money);
    for (size_t b = 0; b < e.buildings.size(); ++b)
        w += r.resale * static_cast<double>(e.buildings[b].cost) * owned[b];
    for (size_t g = 0; g < e.goods.size(); ++g)
        w += e.goods[g].base_price * static_cast<double>(stock[g]);
    return static_cast<int64_t>(std::llround(w));
}

void start(Company& c, const Economy& e, const Rules& r, const std::string& name, int strategy,
           uint64_t seed) {
    c = Company{};
    c.name = name;
    c.strategy = strategy;
    c.money = r.start_money;
    c.stock.assign(e.goods.size(), 0);
    c.made.assign(e.goods.size(), 0);
    c.saturation.assign(e.goods.size(), 0.0);
    c.demand_factor.assign(e.goods.size(), 1.0);
    c.sell.assign(e.goods.size(), true);
    c.owned.assign(e.buildings.size(), 0);
    c.ever_owned.assign(e.buildings.size(), 0);
    c.unlocked.assign(e.buildings.size(), false);
    for (size_t b = 0; b < e.buildings.size(); ++b)
        c.unlocked[b] =
            !e.buildings[b].has_unlock || (e.buildings[b].unlock.is_number() &&
                                           e.buildings[b].unlock.as_float().value_or(0) != 0);
    c.milestone_day.assign(e.milestones.size(), -1);
    // The market's draws depend on the seed alone, so companies compared on one seed face the
    // same days; the strategy's own draws are its own.
    const uint64_t market = seed * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull;
    c.market_rng = base::Rng::at(market);
    c.rng = base::Rng::at(market ^ (uint64_t(strategy + 2) * 0xBF58476D1CE4E5B9ull));
}

bool build(Company& c, const Economy& e, int building) {
    if (c.bankrupt || building < 0 || building >= int(e.buildings.size()) ||
        !c.unlocked[size_t(building)] || c.money < e.buildings[size_t(building)].cost)
        return false;
    c.money -= e.buildings[size_t(building)].cost;
    c.spent += e.buildings[size_t(building)].cost;
    ++c.owned[size_t(building)];
    c.ever_owned[size_t(building)] =
        std::max(c.ever_owned[size_t(building)], c.owned[size_t(building)]);
    return true;
}

bool demolish(Company& c, const Economy& e, const Rules& r, int building) {
    if (building < 0 || building >= int(e.buildings.size()) || c.owned[size_t(building)] == 0)
        return false;
    --c.owned[size_t(building)];
    c.money +=
        static_cast<int64_t>(std::llround(r.resale * double(e.buildings[size_t(building)].cost)));
    return true;
}

int64_t sell(Company& c, const Economy& e, int good, int64_t units) {
    units = std::min(units, c.stock[size_t(good)]);
    if (units <= 0) return 0;
    // The price falls as the market fills: revenue is the integral of the price curve.
    const Good& g = e.goods[size_t(good)];
    const double d = demand_today(c, g, good);
    double& s = c.saturation[size_t(good)];
    const double revenue = g.base_price * d * std::log((d + s + double(units)) / (d + s));
    s += double(units);
    c.stock[size_t(good)] -= units;
    const auto coins = static_cast<int64_t>(std::llround(revenue));
    c.money += coins;
    c.earned += coins;
    return coins;
}

double daily_profit(const Company& c, const Economy& e, int building) {
    const Building& b = e.buildings[size_t(building)];
    double batch = c.price(e, b.output);
    for (int g : b.inputs) batch -= c.price(e, g);
    return batch * b.rate - b.upkeep;
}

namespace {

// Units of each good tomorrow's production needs.
std::vector<int64_t> needs(const Company& c, const Economy& e) {
    std::vector<int64_t> need(e.goods.size(), 0);
    for (size_t b = 0; b < e.buildings.size(); ++b)
        for (int g : e.buildings[b].inputs)
            need[size_t(g)] += int64_t(c.owned[b]) * e.buildings[b].rate;
    return need;
}

// The buildings that make `good` and, recursively, its inputs.
void chain_of(const Economy& e, int good, std::vector<bool>& in, int depth = 0) {
    if (good < 0 || depth > 8) return;
    for (size_t b = 0; b < e.buildings.size(); ++b)
        if (e.buildings[b].output == good && !in[b]) {
            in[b] = true;
            for (int g : e.buildings[b].inputs) chain_of(e, g, in, depth + 1);
        }
}

// The building that pays back soonest within `horizon` days, among `allowed`, if the
// company can afford it; -1 if there is none, or it must save for it.
int best_payback(const Company& c, const Economy& e, const std::vector<bool>* allowed, int horizon,
                 double reserve) {
    int best = -1;
    double best_days = 0;
    for (size_t b = 0; b < e.buildings.size(); ++b) {
        if (!c.unlocked[b] || (allowed && !(*allowed)[b])) continue;
        // More of what is not selling is no investment: five days' output unsold stops it.
        const Building& bd = e.buildings[b];
        if (c.stock[size_t(bd.output)] > 5 * int64_t(bd.rate) * std::max(1, c.owned[b])) continue;
        const double cost = static_cast<double>(e.buildings[b].cost);
        const double profit = daily_profit(c, e, int(b));
        if (profit <= 0) continue;
        const double days = cost / profit;
        if (days > horizon) continue;
        if (best < 0 || days < best_days) {
            best = int(b);
            best_days = days;
        }
    }
    // The best investment, if it can afford it with its reserve; otherwise it saves for it.
    if (best >= 0 && static_cast<double>(c.money) <
                         static_cast<double>(e.buildings[size_t(best)].cost) * (1 + reserve))
        return -1;
    return best;
}

void decide(Company& c, const Economy& e) {
    const Strategy& s = e.strategies[size_t(c.strategy)];
    // Anything but the random player stops expanding while it is losing money.
    const auto& w = c.worth_by_day;
    if (s.kind != "random" && w.size() > 30 && w.back() < w[w.size() - 31]) return;
    if (s.kind == "random") {
        if (c.rng.unit() < s.build_chance) {
            std::vector<int> can;
            for (size_t b = 0; b < e.buildings.size(); ++b)
                if (c.unlocked[b] && c.money >= e.buildings[b].cost) can.push_back(int(b));
            if (!can.empty()) build(c, e, can[c.rng.next() % can.size()]);
        }
        return;
    }
    if (s.kind == "specialist") {
        std::vector<bool> in(e.buildings.size(), false);
        chain_of(e, e.good(s.chain), in);
        // In its chain, what pays back; failing that, what feeds the starved step.
        int b = best_payback(c, e, &in, s.horizon, s.reserve);
        if (b < 0) {
            const std::vector<int64_t> need = needs(c, e);
            for (size_t i = 0; i < e.buildings.size() && b < 0; ++i) {
                const int out = e.buildings[i].output;
                if (in[i] && c.unlocked[i] && need[size_t(out)] > c.stock[size_t(out)] &&
                    c.money >= e.buildings[i].cost * static_cast<int64_t>(1 + s.reserve))
                    b = int(i);
            }
        }
        if (b >= 0) build(c, e, b);
        return;
    }
    // greedy and hoarder: the best payback, the hoarder only above its reserve.
    if (int b = best_payback(c, e, nullptr, s.horizon, s.reserve); b >= 0) build(c, e, b);
}

}  // namespace

void day(Company& c, const Economy& e, const Rules& r) {
    ++c.day;
    if (c.bankrupt) {
        c.worth_by_day.push_back(c.worth(e, r));
        return;
    }
    // Production, building by building, as far as the inputs go.
    for (size_t b = 0; b < e.buildings.size(); ++b) {
        const Building& bd = e.buildings[b];
        int64_t batches = int64_t(c.owned[b]) * bd.rate;
        for (int g : bd.inputs) {
            const auto per = std::count(bd.inputs.begin(), bd.inputs.end(), g);
            batches = std::min<int64_t>(batches, c.stock[size_t(g)] / per);
        }
        if (batches <= 0) continue;
        for (int g : bd.inputs) c.stock[size_t(g)] -= batches;
        c.stock[size_t(bd.output)] += batches;
        c.made[size_t(bd.output)] += batches;
    }
    for (size_t g = 0; g < e.goods.size(); ++g)
        if (e.goods[g].spoil_days > 0) c.stock[g] -= c.stock[g] / e.goods[g].spoil_days;

    if (c.strategy >= 0) decide(c, e);

    // Sales: everything but what tomorrow's production needs. A hoarder waits for a price,
    // but not past ten days of its own output: a warehouse is not a strategy.
    const std::vector<int64_t> need = needs(c, e);
    const Strategy* s = c.strategy >= 0 ? &e.strategies[size_t(c.strategy)] : nullptr;
    for (size_t g = 0; g < e.goods.size(); ++g) {
        if (!c.sell[g]) continue;
        int64_t output = 0;
        for (size_t b = 0; b < e.buildings.size(); ++b)
            if (e.buildings[b].output == int(g))
                output += int64_t(c.owned[b]) * e.buildings[b].rate;
        if (s && s->sell_above > 0 && c.stock[g] <= 10 * output &&
            c.price(e, int(g)) < s->sell_above * e.goods[g].base_price)
            continue;
        sell(c, e, int(g), c.stock[g] - need[g]);
    }

    double upkeep = 0;
    for (size_t b = 0; b < e.buildings.size(); ++b) upkeep += e.buildings[b].upkeep * c.owned[b];
    const auto paid = static_cast<int64_t>(std::llround(upkeep));
    c.money -= paid;
    c.spent += paid;

    for (size_t g = 0; g < e.goods.size(); ++g) {
        // The market clears a share of what was sold; tomorrow's demand is drawn.
        c.saturation[g] *= 1.0 - r.recovery;
        c.demand_factor[g] = 1.0 + e.goods[g].volatility * (2.0 * c.market_rng.unit() - 1.0);
    }
    if (c.money < 0 && s && s->kind != "random") {
        int worst = -1;
        for (size_t b = 0; b < e.buildings.size(); ++b)
            if (c.owned[b] > 0 &&
                (worst < 0 || daily_profit(c, e, int(b)) < daily_profit(c, e, worst)))
                worst = int(b);
        demolish(c, e, r, worst);
    }
    c.debt_days = c.money < 0 ? c.debt_days + 1 : 0;
    if (c.debt_days >= r.bankrupt_days) c.bankrupt = true;
    c.worth_by_day.push_back(c.worth(e, r));
}

}  // namespace ledger
