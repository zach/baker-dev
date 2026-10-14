// Ledger (game-stacks.md §4.2): a tycoon economy. The economy is economy.hpp; this file is
// the game around it: the in-game clock, the script words (econ.), the mouse UI, and a
// town drawn from what you own.
//
//   stacks_ledger                      play: build, sell, and watch the money
//   stacks_ledger sim --seeds 1-10     the bot strategies against each other, judged by metrics/
//   stacks_ledger eval "{econ.compete}" "{econ.advance_days 365}" "{econ.leader}"
//
// The numbers come from economy.xlsx through tools/export_economy.py, which writes
// data/economy.dta; tune in data/agent.dta and report which sheet cells to change.
#include "economy.hpp"

#include <base/app.hpp>
#include <base/render.hpp>
#include <base/words.hpp>

#include <imgui.h>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static baker::Tweak<float> kDaySeconds{
    "econ.day_seconds", 5.0f, {.range = {0.05f, 600}, .desc = "Real seconds per in-game day"}};
static baker::Tweak<int> kStartMoney{
    "econ.start_money", 1000, {.range = {0, 1000000}, .desc = "Coins a company starts with"}};
static baker::Tweak<float> kRecovery{
    "econ.recovery",
    0.25f,
    {.range = {0.01f, 1}, .desc = "Share of a market's glut cleared daily"}};
static baker::Tweak<float> kResale{
    "econ.resale", 0.5f, {.range = {0, 1}, .desc = "A building's worth, as a share of its cost"}};
static baker::Tweak<int> kBankruptDays{
    "econ.bankrupt_days", 30, {.range = {1, 3650}, .desc = "Days in debt before bankruptcy"}};

namespace {

ledger::Rules current_rules() {
    ledger::Rules r;
    r.start_money = kStartMoney;
    r.recovery = kRecovery;
    r.resale = kResale;
    r.bankrupt_days = kBankruptDays;
    return r;
}

class LedgerGame final : public base::Game {
public:
    void bind(baker::Context& ctx) override {
        ctx_ = &ctx;
        auto doc = [](auto f, const char* text) {
            if (f) f->doc(text);
        };
        doc(ctx.func("econ.play", [this] { restart(false); }),
            "One company, played by you (the default).");
        doc(ctx.func("econ.compete", [this] { restart(true); }),
            "One company per strategy in (strategies ...), each in a market of its own with "
            "the same days, from the same start.");
        doc(ctx.func("econ.reset", [this] { restart(competing_); }),
            "Starts the companies over, keeping play or compete.");
        doc(ctx.func("econ.advance_days",
                     [this](int64_t n) {
                         for (int64_t i = 0; i < n; ++i) advance_day();
                     }),
            "Runs this many days now.");
        doc(ctx.func("econ.grant", [this](int64_t n) { company().money += n; }),
            "Gives the current company coins.");
        doc(ctx.func("econ.drain", [this](int64_t n) { company().money -= n; }),
            "Takes coins from the current company.");
        doc(ctx.func("econ.day", [this] { return int64_t{company().day}; }),
            "The current company's day.");
        doc(ctx.func("econ.money", [this] { return company().money; }),
            "The current company's coins.");
        doc(ctx.func("econ.total_earned", [this] { return company().earned; }),
            "Coins the current company has made from sales, ever.");
        doc(ctx.func("econ.count",
                     [this](baker::Symbol b) -> baker::Expected<int64_t> {
                         const int i = econ_.building(std::string(b.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no building " + std::string(b.str())));
                         return company().owned[size_t(i)];
                     }),
            "How many of a building the current company owns.");
        doc(ctx.func("econ.made",
                     [this](baker::Symbol g) -> baker::Expected<int64_t> {
                         const int i = econ_.good(std::string(g.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no good " + std::string(g.str())));
                         return company().made[size_t(i)];
                     }),
            "Units of a good the current company has made, ever.");
        doc(ctx.func("econ.price",
                     [this](baker::Symbol g) -> baker::Expected<double> {
                         const int i = econ_.good(std::string(g.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no good " + std::string(g.str())));
                         return company().price(econ_, i);
                     }),
            "A good's price today in the current company's market.");
        doc(ctx.func("econ.build",
                     [this](baker::Symbol b) -> baker::Expected<bool> {
                         const int i = econ_.building(std::string(b.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no building " + std::string(b.str())));
                         return ledger::build(company(), econ_, i);
                     }),
            "Buys a building for the current company; 0 if locked or unaffordable.");
        doc(ctx.func("econ.demolish",
                     [this](baker::Symbol b) -> baker::Expected<bool> {
                         const int i = econ_.building(std::string(b.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no building " + std::string(b.str())));
                         return ledger::demolish(company(), econ_, current_rules(), i);
                     }),
            "Sells one of the current company's buildings back at its resale value.");
        doc(ctx.func("econ.worth",
                     [this](baker::Symbol who) -> baker::Expected<int64_t> {
                         auto c = find(who);
                         if (!c) return baker::Unexpected(c.error());
                         return (*c)->worth(econ_, current_rules());
                     }),
            "A company's net worth: coins, buildings at resale, stock at base price.");
        doc(ctx.func("econ.milestone_day",
                     [this](baker::Symbol who, baker::Symbol m) -> baker::Expected<int64_t> {
                         auto c = find(who);
                         if (!c) return baker::Unexpected(c.error());
                         for (size_t i = 0; i < econ_.milestones.size(); ++i)
                             if (econ_.milestones[i].name == m.str()) return (*c)->milestone_day[i];
                         return baker::Unexpected(
                             base::not_found("no milestone " + std::string(m.str())));
                     }),
            "The day a company reached a milestone of (milestones ...); -1 if it has not.");
        doc(ctx.func("econ.milestones_late",
                     [this](baker::Symbol who) -> baker::Expected<int64_t> {
                         auto c = find(who);
                         if (!c) return baker::Unexpected(c.error());
                         int64_t late = 0;
                         for (size_t i = 0; i < econ_.milestones.size(); ++i) {
                             const int d = (*c)->milestone_day[i];
                             if (d < 0 || d > econ_.milestones[i].target_day) ++late;
                         }
                         return late;
                     }),
            "Milestones a company reached after their target day, or not at all.");
        doc(ctx.func("econ.leader", [this] { return baker::Symbol(ranked(0)->name); }),
            "The company with the highest net worth.");
        doc(ctx.func("econ.lead_ratio",
                     [this] {
                         const auto r = current_rules();
                         const ledger::Company* a = ranked(0);
                         const ledger::Company* b = ranked(1);
                         if (!b) return 1.0;
                         const double second = std::max<double>(1.0, double(b->worth(econ_, r)));
                         return double(a->worth(econ_, r)) / second;
                     }),
            "The leader's net worth over the runner-up's: a strategy that runs away shows here.");
        doc(ctx.func("econ.unused_buildings",
                     [this] {
                         int64_t unused = 0;
                         for (size_t b = 0; b < econ_.buildings.size(); ++b)
                             if (std::none_of(
                                     companies_.begin(), companies_.end(),
                                     [&](const ledger::Company& c) { return c.ever_owned[b] > 0; }))
                                 ++unused;
                         return unused;
                     }),
            "Building kinds no company has ever owned: dead content.");
        doc(ctx.func("econ.bankruptcies",
                     [this] {
                         int64_t n = 0;
                         for (const ledger::Company& c : companies_)
                             if (c.bankrupt &&
                                 !(c.strategy >= 0 &&
                                   econ_.strategies[size_t(c.strategy)].kind == "random"))
                                 ++n;
                         return n;
                     }),
            "Companies gone bankrupt, not counting those that build at random: dead ends.");
        doc(ctx.func("econ.report", [this] { return report(); }),
            "Every company: coins, worth, buildings owned, milestones reached, as a table.");
        doc(ctx.func("econ.growth",
                     [this](baker::Symbol who, int64_t days) -> baker::Expected<double> {
                         auto c = find(who);
                         if (!c) return baker::Unexpected(c.error());
                         const auto& w = (*c)->worth_by_day;
                         if (days <= 0 || int64_t(w.size()) <= days) return 0.0;
                         const double before =
                             std::max<double>(1.0, double(w[w.size() - 1 - size_t(days)]));
                         return double(w.back()) / before;
                     }),
            "A company's net worth now over its worth `days` ago: above 1 grows.");
    }

    void load(const baker::Config& cfg) override {
        ledger::Economy next;
        std::string error;
        if (!ledger::load(cfg.root(), next, error)) {
            std::fprintf(stderr, "ledger: %s; keeping the last good economy\n", error.c_str());
            return;
        }
        const bool same_shape = loaded_ && next.goods.size() == econ_.goods.size() &&
                                next.buildings.size() == econ_.buildings.size() &&
                                next.milestones.size() == econ_.milestones.size() &&
                                next.strategies.size() == econ_.strategies.size();
        econ_ = std::move(next);
        loaded_ = true;
        // New numbers keep the game going; new goods or buildings start it over.
        if (!same_shape) restart(competing_);
    }

    void reset(uint64_t seed) override {
        seed_ = seed;
        restart(false);
    }

    bool start(base::App&) override {
        if (!loaded_) std::fprintf(stderr, "ledger: the data has no usable economy\n");
        return loaded_;
    }

    void step(double dt) override {
        if (!loaded_ || paused_) return;
        // Days are counted in steps, not summed seconds, so a run is the same at any rate.
        const auto per_day = std::max<int64_t>(1, std::llround(double(kDaySeconds) / dt / speed_));
        if (++steps_ >= per_day) {
            steps_ = 0;
            advance_day();
        }
    }

    void draw(base::Renderer& r, float) override {
        r.clear_color(0.55f, 0.70f, 0.85f);
        base::Camera cam;
        cam.eye[0] = 0;
        cam.eye[1] = 9;
        cam.eye[2] = -14;
        cam.at[1] = 0;
        r.camera(cam);
        r.box(0, -0.05f, 0, 30, 0.1f, 30, 0.35f, 0.55f, 0.3f);
        if (!loaded_ || companies_.empty()) return;
        // The town: one row per kind of building, one lot per building owned.
        const ledger::Company& c = companies_[0];
        for (size_t b = 0; b < econ_.buildings.size(); ++b) {
            const float hue = float(b) / float(std::max<size_t>(1, econ_.buildings.size()));
            const float cr = 0.5f + 0.5f * std::sin(6.28f * hue);
            const float cg = 0.5f + 0.5f * std::sin(6.28f * (hue + 0.33f));
            const float cb = 0.5f + 0.5f * std::sin(6.28f * (hue + 0.66f));
            const int n = std::min(c.owned[b], 24);
            const float height = 0.6f + 0.15f * float(econ_.buildings[b].inputs.size());
            for (int i = 0; i < n; ++i)
                r.box(-11.0f + float(i), height / 2, -6.0f + 2.2f * float(b), 0.8f, height, 1.4f,
                      cr, cg, cb);
        }
        r.text(48, 3, "%s  day %d  coins %lld  %s", c.name.c_str(), c.day,
               static_cast<long long>(c.money), c.bankrupt ? "BANKRUPT" : "");
    }

    void ui() override {
        if (!loaded_ || companies_.empty()) return;
        ImGui::SetNextWindowPos(ImVec2(380, 40), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560, 470), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Ledger")) {
            ImGui::End();
            return;
        }
        if (competing_) {
            for (size_t i = 0; i < companies_.size(); ++i) {
                if (i) ImGui::SameLine();
                if (ImGui::RadioButton(companies_[i].name.c_str(), shown_ == i)) shown_ = i;
            }
        }
        shown_ = std::min(shown_, companies_.size() - 1);
        ledger::Company& c = companies_[shown_];
        const ledger::Rules rules = current_rules();
        ImGui::Text("Day %d   Coins %lld   Worth %lld   Earned %lld", c.day,
                    static_cast<long long>(c.money), static_cast<long long>(c.worth(econ_, rules)),
                    static_cast<long long>(c.earned));
        if (ImGui::Button(paused_ ? "Resume" : "Pause")) paused_ = !paused_;
        for (double s : {1.0, 4.0, 16.0}) {
            ImGui::SameLine();
            char label[16];
            std::snprintf(label, sizeof label, "%gx", s);
            if (ImGui::RadioButton(label, speed_ == s)) speed_ = s;
        }
        ImGui::SameLine();
        if (ImGui::Button("+30 days"))
            for (int i = 0; i < 30; ++i) advance_day();

        if (c.worth_by_day.size() > 1) {
            const size_t n = std::min<size_t>(c.worth_by_day.size(), 365);
            std::vector<float> worth(n);
            for (size_t i = 0; i < n; ++i)
                worth[i] = float(c.worth_by_day[c.worth_by_day.size() - n + i]);
            ImGui::PlotLines("worth", worth.data(), int(n), 0, "net worth, last year", FLT_MAX,
                             FLT_MAX, ImVec2(0, 70));
        }

        if (ImGui::BeginTable("goods", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            for (const char* h : {"good", "stock", "price", "base", "sell"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (size_t g = 0; g < econ_.goods.size(); ++g) {
                ImGui::PushID(int(g));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(econ_.goods[g].name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(c.stock[g]));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", c.price(econ_, int(g)));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", econ_.goods[g].base_price);
                ImGui::TableNextColumn();
                bool sell = c.sell[g];
                if (ImGui::Checkbox("##sell", &sell)) c.sell[g] = sell;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("buildings", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            for (const char* h : {"building", "owned", "cost", "upkeep", "profit/day", "", ""})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (size_t b = 0; b < econ_.buildings.size(); ++b) {
                const ledger::Building& bd = econ_.buildings[b];
                ImGui::PushID(int(b));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                std::string recipe;
                for (int g : bd.inputs) recipe += econ_.goods[size_t(g)].name + " ";
                recipe += "-> " + econ_.goods[size_t(bd.output)].name;
                ImGui::TextUnformatted(bd.name.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s, %d a day", recipe.c_str(), bd.rate);
                ImGui::TableNextColumn();
                ImGui::Text("%d", c.owned[b]);
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(bd.cost));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", bd.upkeep);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", ledger::daily_profit(c, econ_, int(b)));
                ImGui::TableNextColumn();
                if (!c.unlocked[b]) {
                    ImGui::TextDisabled("locked");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", baker::to_string(bd.unlock).c_str());
                } else {
                    ImGui::BeginDisabled(c.money < bd.cost || c.strategy >= 0);
                    if (ImGui::Button("Build")) ledger::build(c, econ_, int(b));
                    ImGui::EndDisabled();
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(c.owned[b] == 0 || c.strategy >= 0);
                if (ImGui::Button("Sell")) ledger::demolish(c, econ_, rules, int(b));
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        for (size_t m = 0; m < econ_.milestones.size(); ++m) {
            const ledger::Milestone& ms = econ_.milestones[m];
            const int d = c.milestone_day[m];
            if (d >= 0)
                ImGui::Text("[x] %s: day %d (target %d)", ms.desc.c_str(), d, ms.target_day);
            else
                ImGui::TextDisabled("[ ] %s (target day %d)", ms.desc.c_str(), ms.target_day);
        }
        ImGui::End();
    }

private:
    void restart(bool compete) {
        competing_ = compete;
        companies_.clear();
        steps_ = 0;
        current_ = 0;
        shown_ = 0;
        if (!loaded_) return;
        const ledger::Rules rules = current_rules();
        if (compete) {
            companies_.resize(econ_.strategies.size());
            for (size_t i = 0; i < econ_.strategies.size(); ++i)
                ledger::start(companies_[i], econ_, rules, econ_.strategies[i].name, int(i), seed_);
        } else {
            companies_.resize(1);
            ledger::start(companies_[0], econ_, rules, "player", -1, seed_);
        }
        for (size_t i = 0; i < companies_.size(); ++i) judge(i);
    }

    std::string report() const {
        const ledger::Rules r = current_rules();
        std::string s = "company      day     coins       worth  ";
        for (const ledger::Building& b : econ_.buildings) s += pad(b.name, 7);
        for (const ledger::Milestone& m : econ_.milestones) s += pad(m.name, 13);
        s += "\n";
        char line[64];
        for (const ledger::Company& c : companies_) {
            std::snprintf(line, sizeof line, "%-10s %5d %9lld %11lld  ", c.name.c_str(), c.day,
                          static_cast<long long>(c.money),
                          static_cast<long long>(c.worth(econ_, r)));
            s += line;
            for (size_t b = 0; b < econ_.buildings.size(); ++b)
                s += pad(std::to_string(c.owned[b]), 7);
            for (int d : c.milestone_day) s += pad(d < 0 ? "-" : std::to_string(d), 13);
            if (c.bankrupt) s += "BANKRUPT";
            s += "\n";
        }
        return s;
    }
    static std::string pad(const std::string& text, size_t width) {
        return text.size() >= width ? text + " " : text + std::string(width - text.size(), ' ');
    }

    ledger::Company& company() { return companies_[current_]; }

    baker::Expected<ledger::Company*> find(baker::Symbol who) {
        for (ledger::Company& c : companies_)
            if (c.name == who.str()) return &c;
        return baker::Unexpected(
            base::not_found("no company " + std::string(who.str()) +
                            " (econ.compete makes one per strategy; econ.play makes player)"));
    }

    const ledger::Company* ranked(size_t place) const {
        std::vector<const ledger::Company*> order;
        for (const ledger::Company& c : companies_) order.push_back(&c);
        const ledger::Rules r = current_rules();
        std::stable_sort(order.begin(), order.end(),
                         [&](auto* a, auto* b) { return a->worth(econ_, r) > b->worth(econ_, r); });
        return place < order.size() ? order[place] : nullptr;
    }

    void advance_day() {
        const ledger::Rules rules = current_rules();
        for (size_t i = 0; i < companies_.size(); ++i) {
            ledger::day(companies_[i], econ_, rules);
            judge(i);
        }
    }

    // The data's commands, about company i: what unlocks, and which milestones it reached.
    void judge(size_t i) {
        if (!ctx_) return;
        const size_t saved = current_;
        current_ = i;
        ledger::Company& c = companies_[i];
        for (size_t b = 0; b < econ_.buildings.size(); ++b)
            if (!c.unlocked[b] && econ_.buildings[b].unlock.type() == baker::Type::Command)
                if (auto r = ctx_->eval(econ_.buildings[b].unlock); r && base::truthy(*r))
                    c.unlocked[b] = true;
        for (size_t m = 0; m < econ_.milestones.size(); ++m)
            if (c.milestone_day[m] < 0)
                if (auto r = ctx_->eval(econ_.milestones[m].when); r && base::truthy(*r))
                    c.milestone_day[m] = c.day;
        current_ = saved;
    }

    baker::Context* ctx_ = nullptr;
    ledger::Economy econ_;
    bool loaded_ = false, competing_ = false, paused_ = false;
    std::vector<ledger::Company> companies_;
    size_t current_ = 0, shown_ = 0;
    uint64_t seed_ = 0;
    int64_t steps_ = 0;
    double speed_ = 1;
};

}  // namespace

int main(int argc, char** argv) {
    LedgerGame game;
    return base::main(game, argc, argv, {.title = "Ledger", .data_dir = LEDGER_DATA});
}
