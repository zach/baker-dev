#include "household.hpp"

#include <base/rng.hpp>
#include <base/words.hpp>
#include <moira/std_interop.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <span>
#include <thread>

namespace hearth {
namespace {

constexpr float kWallHeight = 2.5f;
constexpr float kRadians = 0.017453292f;
constexpr size_t kLogLines = 40;
constexpr size_t kNotes = 12;

// How much a need's deficit matters: 0 when met, 1 when desperate.
float urgency(Curve c, float value) {
    const float d = std::clamp((100 - value) / 100, 0.0f, 1.0f);
    switch (c) {
        case Curve::Linear: return d;
        case Curve::Quadratic: return d * d;
        case Curve::Logistic: return 1 / (1 + std::exp(-10 * (d - 0.5f)));
    }
    return d;
}

std::string clock_text(double minute) {
    const int m = int(minute);
    char buf[32];
    std::snprintf(buf, sizeof buf, "day %d %02d:%02d", m / 1440 + 1, (m / 60) % 24, m % 60);
    return buf;
}

const moira::Symbol& idle_symbol() {
    static const moira::Symbol s("idle");
    return s;
}

}  // namespace

int Kit::need(const std::string& name) const {
    for (size_t i = 0; i < needs.size(); ++i)
        if (needs[i].name == name) return int(i);
    return -1;
}
int Kit::kind(const std::string& name) const {
    for (size_t i = 0; i < kinds.size(); ++i)
        if (kinds[i].name == name) return int(i);
    return -1;
}
int Kit::house(const std::string& name) const {
    for (size_t i = 0; i < houses.size(); ++i)
        if (houses[i].name == name) return int(i);
    return -1;
}

bool load(const moira::Ref<moira::Array>& root, Kit& out, std::string& error) {
    Kit k;
    k.tree = root;
    if (const moira::Array* r = root->try_find(moira::Symbol("hearth"))) {
        Rules& u = k.rules;
        u.walk_speed = base::number(*r, "walk_speed", u.walk_speed);
        u.choice_top = int(base::number(*r, "choice_top", float(u.choice_top)));
        u.choice_within = base::number(*r, "choice_within", u.choice_within);
        u.distance_scale = base::number(*r, "distance_scale", u.distance_scale);
        u.off_hours = base::number(*r, "off_hours", u.off_hours);
        u.stuck_minutes = base::number(*r, "stuck_minutes", u.stuck_minutes);
        u.starve_minutes = base::number(*r, "starve_minutes", u.starve_minutes);
        u.oscillation_switches =
            int(base::number(*r, "oscillation_switches", float(u.oscillation_switches)));
        u.idle_minutes = base::number(*r, "idle_minutes", u.idle_minutes);
    }
    if (const moira::Array* needs = root->try_find(moira::Symbol("needs")))
        for (const moira::Array& a : needs->subarrays()) {
            Need n;
            n.name = base::name(a);
            n.decay_per_hour = std::max(0.0f, base::number(a, "decay_per_hour", n.decay_per_hour));
            n.critical = base::number(a, "critical", n.critical);
            const auto c = a.find_sym(moira::Symbol("curve"), moira::Symbol("linear"));
            const std::string cs = c ? std::string(c->str()) : "linear";
            n.curve = cs == "quadratic"  ? Curve::Quadratic
                      : cs == "logistic" ? Curve::Logistic
                                         : Curve::Linear;
            k.needs.push_back(std::move(n));
        }
    if (k.needs.empty()) {
        error = "no (needs ...) in the data";
        return false;
    }
    if (const moira::Array* kinds = root->try_find(moira::Symbol("objects")))
        for (const moira::Array& a : kinds->subarrays()) {
            Kind kd;
            kd.name = base::name(a);
            for (int i = 0; i < 3; ++i) {
                kd.size[i] = std::max(0.1f, base::number(a, "size", size_t(i), kd.size[i]));
                kd.color[i] = base::number(a, "color", size_t(i), kd.color[i]);
            }
            if (const moira::Array* ad = a.try_find(moira::Symbol("advertise")))
                for (const moira::Array& n : ad->subarrays()) {
                    const int need = k.need(base::name(n));
                    if (need < 0) {
                        error = "object " + kd.name + " advertises " + base::name(n) +
                                ", which is not one of (needs ...)";
                        return false;
                    }
                    kd.advertise.emplace_back(
                        need, float(n.size() > 1 ? n[1].as_float().value_or(0) : 0));
                }
            const auto act = a.find_sym(moira::Symbol("action"), moira::Symbol("use_object"));
            kd.action = act ? *act : moira::Symbol("use_object");
            kd.minutes = std::max(1.0f, base::number(a, "minutes", kd.minutes));
            kd.users = std::max(1, int(base::number(a, "users", 1)));
            kd.hours[0] = base::number(a, "hours", 0, -1);
            kd.hours[1] = base::number(a, "hours", 1, -1);
            k.kinds.push_back(std::move(kd));
        }
    if (const moira::Array* houses = root->try_find(moira::Symbol("households")))
        for (const moira::Array& a : houses->subarrays()) {
            House h;
            h.name = base::name(a);
            if (auto d = a.find_str(moira::Symbol("desc"), "")) h.desc = std::string(*d);
            h.size[0] = std::max(2.0f, base::number(a, "size", 0, h.size[0]));
            h.size[1] = std::max(2.0f, base::number(a, "size", 1, h.size[1]));
            if (const moira::Array* walls = a.try_find(moira::Symbol("walls")))
                for (const moira::Array& w : walls->subarrays()) {
                    std::array<float, 4> box{};
                    for (size_t i = 0; i < 4; ++i)
                        box[i] = float(w.size() > i ? w[i].as_float().value_or(0) : 0);
                    h.walls.push_back(box);
                }
            if (const moira::Array* things = a.try_find(moira::Symbol("things")))
                for (const moira::Array& t : things->subarrays()) {
                    Placed p;
                    p.kind = k.kind(base::name(t));
                    if (p.kind < 0) {
                        error = "household " + h.name + " places " + base::name(t) +
                                ", which is not one of (objects ...)";
                        return false;
                    }
                    p.x = float(t.size() > 1 ? t[1].as_float().value_or(0) : 0);
                    p.z = float(t.size() > 2 ? t[2].as_float().value_or(0) : 0);
                    p.yaw = float(t.size() > 3 ? t[3].as_float().value_or(0) : 0);
                    h.things.push_back(p);
                }
            if (const moira::Array* rs = a.try_find(moira::Symbol("residents")))
                for (const moira::Array& r : rs->subarrays()) {
                    Resident res;
                    res.name = base::name(r);
                    res.weights.assign(k.needs.size(), 1.0f);
                    if (const moira::Array* w = r.try_find(moira::Symbol("weights")))
                        for (const moira::Array& n : w->subarrays()) {
                            const int need = k.need(base::name(n));
                            if (need < 0) {
                                error = "resident " + res.name + " weighs " + base::name(n) +
                                        ", which is not one of (needs ...)";
                                return false;
                            }
                            res.weights[size_t(need)] =
                                float(n.size() > 1 ? n[1].as_float().value_or(1) : 1);
                        }
                    for (int i = 0; i < 3; ++i)
                        res.color[i] = base::number(r, "color", size_t(i), res.color[i]);
                    h.residents.push_back(std::move(res));
                }
            k.houses.push_back(std::move(h));
        }
    if (k.houses.empty()) {
        error = "no (households ...) in the data";
        return false;
    }
    out = std::move(k);
    return true;
}

// --- Thing ---

void Thing::bind(moira::Binder<Thing>& b) {
    b.name("thing");
    b.method("kind", [](Thing& t) {
         return moira::Symbol(t.home->kit().kinds[size_t(t.kind)].name);
     }).doc("The thing's kind, from (objects ...).");
    b.method("users", [](Thing& t) {
         return int64_t(t.users);
     }).doc("How many villagers are using it, or on their way to.");
    b.method("uses", [](Thing& t) { return int64_t(t.uses); }).doc("How often it has been used.");
}

// --- Villager ---

void Villager::bind(moira::Binder<Villager>& b) {
    b.name("villager");
    b.method("walk_to", &Villager::walk_to)
        .doc("Starts walking to a thing. False, and the action ends, if it cannot be reached.");
    b.method("arrived", &Villager::arrived).doc("Whether the walk has ended at the thing.");
    b.method("use", &Villager::use)
        .doc("Starts using a thing for its minutes; its needs rise as it advertises.");
    b.method("used", &Villager::used).doc("Whether the use has run its minutes.");
    b.method("done", &Villager::done).doc("Ends the action, finished: the villager decides again.");
    b.method("interrupt", &Villager::interrupt)
        .doc("Ends the action unfinished, for a need: the villager decides again.");
    b.method("helps", &Villager::helps)
        .doc("Whether the current action's thing advertises this need.");
    b.method("need", &Villager::need).doc("A need's value, 0 (desperate) to 100 (met).");
    b.method("say", &Villager::say).doc("Writes a line in the villager's behaviour log.");
    b.on<NeedCritical>(&Villager::on_need_critical)
        .doc(
            "A need fell below its critical value. By default, interrupts unless the current "
            "action helps it.");
}

bool Villager::walk_to(const moira::Node& obj) {
    Thing* t = home->thing_of(obj);
    walked = false;
    if (!t || !t->reachable) {
        home->note(*this, "cannot reach " + (t ? home->kit().kinds[size_t(t->kind)].name : "that"));
        if (t) t->reachable = false;
        home->end_action(*this, false);
        return false;
    }
    std::vector<base::NavPoint> p;
    if (!home->nav().path({x, 0, z}, {t->spot[0], t->spot[1], t->spot[2]}, p) || p.empty()) {
        home->note(*this, "no path to " + home->kit().kinds[size_t(t->kind)].name);
        t->reachable = false;
        home->end_action(*this, false);
        return false;
    }
    path = std::move(p);
    path_at = 0;
    state = State::Walking;
    last_progress = home->minute_now();
    return true;
}

bool Villager::use(const moira::Node& obj) {
    Thing* t = home->thing_of(obj);
    if (!t) return false;
    state = State::Using;
    use_left = home->kit().kinds[size_t(t->kind)].minutes;
    ++t->uses;
    last_progress = home->minute_now();
    return true;
}

void Villager::done() {
    home->end_action(*this, true);
}

void Villager::interrupt(moira::Symbol n) {
    if (thing < 0) return;
    home->note(*this, std::string("interrupted for ") + std::string(n.str()));
    home->end_action(*this, false);
}

bool Villager::helps(moira::Symbol n) const {
    if (thing < 0) return false;
    const int need_index = home->kit().need(std::string(n.str()));
    const Thing& t = *home->things()[size_t(thing)];
    for (const auto& [need_i, amount] : home->kit().kinds[size_t(t.kind)].advertise)
        if (need_i == need_index && amount > 0) return true;
    return false;
}

double Villager::need(moira::Symbol n) const {
    const int i = home->kit().need(std::string(n.str()));
    return i < 0 ? -1.0 : double(needs[size_t(i)]);
}

void Villager::say(const std::string& text) {
    home->note(*this, text);
}

void Villager::on_need_critical(const NeedCritical& m) {
    if (!helps(m.need)) interrupt(m.need);
}

// --- Tally ---

void Tally::add(const Tally& o) {
    characters += o.characters;
    character_days += o.character_days;
    stuck += o.stuck;
    oscillating += o.oscillating;
    starving += o.starving;
    things += o.things;
    things_used += o.things_used;
    if (uses_by_kind.size() < o.uses_by_kind.size()) uses_by_kind.resize(o.uses_by_kind.size());
    for (size_t i = 0; i < o.uses_by_kind.size(); ++i) uses_by_kind[i] += o.uses_by_kind[i];
    entropy.insert(entropy.end(), o.entropy.begin(), o.entropy.end());
    steps += o.steps;
    decisions += o.decisions;
    interrupts += o.interrupts;
    script_errors += o.script_errors;
    for (const std::string& n : o.notes)
        if (notes.size() < kNotes) notes.push_back(n);
}

double share_in_band(const Tally& t, double low, double high) {
    if (t.entropy.empty()) return 0;
    size_t in = 0;
    for (double e : t.entropy) in += e >= low && e <= high;
    return double(in) / double(t.entropy.size());
}

// --- Household ---

Household::Household(const Kit& kit, int house, moira::Context& ctx, moira::Scheduler& clock,
                     uint64_t seed, const std::string& prefix)
    : kit_(kit),
      house_(house),
      ctx_(ctx),
      clock_(clock),
      rng_(seed + uint64_t(house) * 0x632BE59BD9B4E019ull, 0x5851F42D4C957F2Dull) {
    clock_node_ = ctx_.object(moira::Symbol("clock"));
    const House& h = this->house();
    build_nav();
    std::vector<int> per_kind(kit_.kinds.size(), 0);
    for (size_t i = 0; i < h.things.size(); ++i) {
        const Placed& p = h.things[i];
        auto t = std::make_unique<Thing>();
        t->home = this;
        t->index = int(i);
        t->kind = p.kind;
        t->x = p.x;
        t->z = p.z;
        t->yaw = p.yaw;
        const Kind& k = kit_.kinds[size_t(p.kind)];
        // Its user stands in front of it: +z turned by its yaw.
        const float out = k.size[2] / 2 + 0.45f;
        const base::NavPoint front{p.x + std::sin(p.yaw * kRadians) * out, 0,
                                   p.z + std::cos(p.yaw * kRadians) * out};
        base::NavPoint at;
        t->reachable = nav_.nearest(front, at, 0.8f);
        t->spot[0] = at.x;
        t->spot[1] = at.y;
        t->spot[2] = at.z;
        std::string name;
        if (!prefix.empty()) {
            name = prefix + k.name;
            if (per_kind[size_t(p.kind)]++ > 0) name += std::to_string(per_kind[size_t(p.kind)]);
        }
        (void)ctx_.add(*t, name.empty() ? moira::Symbol() : moira::Symbol(name));
        things_.push_back(std::move(t));
    }
    for (size_t i = 0; i < h.residents.size(); ++i) {
        auto v = std::make_unique<Villager>();
        v->home = this;
        v->index = int(i);
        v->resident = h.residents[i];
        v->name = h.residents[i].name;
        base::NavPoint at{};
        const float sx = (float(i) - float(h.residents.size() - 1) / 2) * 0.9f;
        if (!nav_.nearest({sx, 0, 0}, at, 3)) at = {sx, 0, 0};
        v->x = at.x;
        v->z = at.z;
        for (size_t n = 0; n < kit_.needs.size(); ++n)
            v->needs.push_back(float(55 + 40 * rng_.unit()));
        v->zero_minutes.assign(kit_.needs.size(), 0);
        v->below.assign(kit_.needs.size(), false);
        v->action = idle_symbol();
        (void)ctx_.add(*v, prefix.empty() ? moira::Symbol() : moira::Symbol(prefix + v->name));
        villagers_.push_back(std::move(v));
    }
    hours_.resize(villagers_.size());
    for (const Kind& k : kit_.kinds) kind_symbols_.emplace_back(k.name);
    tally_.characters = int(villagers_.size());
    tally_.things = int(things_.size());
    tally_.uses_by_kind.assign(kit_.kinds.size(), 0);
}

Household::~Household() {
    for (auto& v : villagers_)
        if (v->task) clock_.cancel(moira::TaskHandle{uint64_t(v->task)});
}

void Household::build_nav() {
    const House& h = house();
    base::NavGeometry g;
    g.box({0, -0.1f, 0}, {h.size[0] / 2, 0.1f, h.size[1] / 2});
    for (const auto& w : h.walls)
        g.box({w[0], kWallHeight / 2, w[1]}, {w[2] / 2, kWallHeight / 2, w[3] / 2});
    for (const Placed& p : h.things) {
        const Kind& k = kit_.kinds[size_t(p.kind)];
        g.box({p.x, k.size[1] / 2, p.z}, {k.size[0] / 2, k.size[1] / 2, k.size[2] / 2},
              p.yaw * kRadians);
    }
    const float tile = std::max(h.size[0], h.size[1]) + 2;
    base::NavSettings s;
    s.tile_size = tile;
    s.origin_x = -tile / 2;
    s.origin_z = -tile / 2;
    s.cell_size = 0.15f;
    s.agent_radius = 0.25f;
    s.bottom = -2;
    s.top = 4;
    s.max_tiles = 1;
    if (nav_.init(s)) (void)nav_.add_tile(0, 0, g);
}

Thing* Household::thing_of(const moira::Node& obj) const {
    Thing* t = ctx_.try_get<Thing>(obj);
    return t && t->home == this ? t : nullptr;
}

void Household::note(Villager& v, std::string text) {
    v.log.push_back(clock_text(minute_) + "  " + std::move(text));
    if (v.log.size() > kLogLines) v.log.erase(v.log.begin());
}

void Household::end_action(Villager& v, bool finished) {
    if (v.task) {
        clock_.cancel(moira::TaskHandle{uint64_t(v.task)});
        v.task = 0;
    }
    if (v.thing >= 0) {
        Thing& t = *things_[size_t(v.thing)];
        t.users = std::max(0, t.users - 1);
    }
    if (finished) {
        v.interrupted.clear();
    } else if (v.thing >= 0) {
        ++tally_.interrupts;
        v.interrupted.push_back(kind_symbols_[size_t(things_[size_t(v.thing)]->kind)]);
        // Back and forth between two actions, never finishing either: oscillating.
        const size_t n = size_t(std::max(2, kit_.rules.oscillation_switches));
        if (v.interrupted.size() >= n) {
            const auto& h = v.interrupted;
            const moira::Symbol a = h[h.size() - 1], b = h[h.size() - 2];
            bool alternates = a != b;
            for (size_t i = h.size() - n; alternates && i < h.size(); ++i)
                alternates = h[i] == ((h.size() - 1 - i) % 2 == 0 ? a : b);
            if (alternates) {
                ++tally_.oscillating;
                if (tally_.notes.size() < kNotes)
                    tally_.notes.push_back(clock_text(minute_) + " " + house().name + " " + v.name +
                                           " oscillates between " + std::string(a.str()) + " and " +
                                           std::string(b.str()));
                v.interrupted.clear();
            }
        }
    }
    v.thing = -1;
    v.action = idle_symbol();
    v.state = Villager::State::Idle;
    v.idle_until = minute_;
    v.path.clear();
    v.walked = false;
    v.use_left = 0;
    v.last_progress = minute_;
}

float Household::score(const Villager& v, const Thing& t) const {
    const Kind& k = kit_.kinds[size_t(t.kind)];
    if (!t.reachable || t.users >= k.users) return 0;
    float gain = 0;
    for (const auto& [n, amount] : k.advertise) {
        const Need& need = kit_.needs[size_t(n)];
        const float now = v.needs[size_t(n)];
        const float after = std::min(100.0f, now + amount);
        gain +=
            v.resident.weights[size_t(n)] * (urgency(need.curve, now) - urgency(need.curve, after));
    }
    if (gain <= 0) return 0;
    if (k.hours[0] >= 0) {
        const float hour = float(std::fmod(minute_ / 60, 24.0));
        const bool in = k.hours[0] <= k.hours[1] ? hour >= k.hours[0] && hour < k.hours[1]
                                                 : hour >= k.hours[0] || hour < k.hours[1];
        if (!in) gain *= kit_.rules.off_hours;
    }
    const float d = std::hypot(t.spot[0] - v.x, t.spot[2] - v.z);
    return gain / (1 + d / std::max(1.0f, kit_.rules.distance_scale));
}

void Household::decide(Villager& v, const std::string& why) {
    ++tally_.decisions;
    Decision dec;
    dec.minute = minute_;
    dec.why = why;
    // A need below critical overrides the routine: while one is, only the things that help
    // a critical need are options, if any of them can be had.
    std::vector<bool> critical(kit_.needs.size(), false);
    bool any_critical = false;
    for (size_t n = 0; n < kit_.needs.size(); ++n)
        any_critical |= critical[n] = v.needs[n] < kit_.needs[n].critical;
    auto helps_critical = [&](const Thing& t) {
        for (const auto& [n, amount] : kit_.kinds[size_t(t.kind)].advertise)
            if (amount > 0 && critical[size_t(n)]) return true;
        return false;
    };
    for (int pass = any_critical ? 0 : 1; pass < 2 && dec.options.empty(); ++pass)
        for (size_t i = 0; i < things_.size(); ++i) {
            if (pass == 0 && !helps_critical(*things_[i])) continue;
            const float s = score(v, *things_[i]);
            if (s > 0) dec.options.push_back({int(i), s});
        }
    std::stable_sort(
        dec.options.begin(), dec.options.end(),
        [](const Decision::Option& a, const Decision::Option& b) { return a.score > b.score; });
    const Rules& r = kit_.rules;
    if (dec.options.empty() || dec.options[0].score < 1e-4f) {
        dec.options.insert(dec.options.begin(), {-1, 0});
        dec.chosen = 0;
        v.last = std::move(dec);
        v.state = Villager::State::Idle;
        v.idle_until = minute_ + r.idle_minutes;
        v.last_progress = minute_;
        return;
    }
    // A weighted pick among the best few, so a routine has some give.
    const float best = dec.options[0].score;
    size_t n = 0;
    float total = 0;
    while (n < dec.options.size() && int(n) < std::max(1, r.choice_top) &&
           dec.options[n].score >= best * r.choice_within)
        total += dec.options[n++].score;
    float pick = float(rng_.unit()) * total;
    size_t c = 0;
    while (c + 1 < n && pick >= dec.options[c].score) pick -= dec.options[c++].score;
    dec.chosen = int(c);
    Thing& t = *things_[size_t(dec.options[c].thing)];
    v.last = std::move(dec);
    ++t.users;
    ++tally_.uses_by_kind[size_t(t.kind)];
    v.thing = t.index;
    v.action = kit_.kinds[size_t(t.kind)].action;
    v.state = Villager::State::Deciding;
    v.walked = false;
    v.last_progress = minute_;
    note(v, why + ": " + kit_.kinds[size_t(t.kind)].name);
    const moira::Node arg = t.node();
    auto started = ctx_.call(v.node(), v.action, std::span<const moira::Node>(&arg, 1));
    if (!started) {
        ++tally_.script_errors;
        if (tally_.notes.size() < kNotes)
            tally_.notes.push_back(house().name + " " + v.name + ": " +
                                   std::string(moira::describe(started.error())));
        note(v, "the action failed: " + std::string(moira::describe(started.error())));
        v.interrupted.clear();
        end_action(v, true);
        v.idle_until = minute_ + r.idle_minutes;
        return;
    }
    if (v.thing >= 0) v.task = started->as_int().value_or(0);
}

void Household::walk(Villager& v, float seconds) {
    float left = kit_.rules.walk_speed * seconds;
    while (left > 0 && v.path_at < v.path.size()) {
        const base::NavPoint& p = v.path[v.path_at];
        const float dx = p.x - v.x, dz = p.z - v.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d <= left) {
            v.x = p.x;
            v.z = p.z;
            left -= d;
            ++v.path_at;
        } else {
            v.x += dx / d * left;
            v.z += dz / d * left;
            left = 0;
        }
    }
    v.last_progress = minute_;
    if (v.path_at >= v.path.size()) {
        v.walked = true;
        v.state = Villager::State::Waiting;
    }
}

void Household::record_hour(Villager& v) {
    const int hour = int(std::fmod(minute_ / 60, 24.0));
    // What the villager is about: the kind of thing it is with, not the handler (many kinds
    // share use_object).
    const moira::Symbol doing =
        v.thing >= 0 ? kind_symbols_[size_t(things_[size_t(v.thing)]->kind)] : idle_symbol();
    auto& counts = hours_[size_t(v.index)][size_t(hour)];
    for (auto& [what, n] : counts)
        if (what == doing) {
            ++n;
            return;
        }
    counts.emplace_back(doing, 1);
}

void Household::step(double seconds, bool tick) {
    const double minutes = seconds / 60;
    minute_ += minutes;
    const Rules& r = kit_.rules;
    for (auto& vp : villagers_) {
        Villager& v = *vp;
        for (size_t n = 0; n < kit_.needs.size(); ++n) {
            const Need& need = kit_.needs[n];
            float& value = v.needs[n];
            value = std::max(0.0f, value - need.decay_per_hour * float(minutes) / 60);
            if (value <= 0) {
                const float before = v.zero_minutes[n];
                v.zero_minutes[n] += float(minutes);
                if (before < r.starve_minutes && v.zero_minutes[n] >= r.starve_minutes) {
                    ++tally_.starving;
                    if (tally_.notes.size() < kNotes)
                        tally_.notes.push_back(clock_text(minute_) + " " + house().name + " " +
                                               v.name + " starves of " + need.name);
                }
            } else {
                v.zero_minutes[n] = 0;
            }
        }
        if (v.state == Villager::State::Using) {
            const Kind& k = kit_.kinds[size_t(things_[size_t(v.thing)]->kind)];
            const float m = std::min(float(minutes), std::max(0.0f, v.use_left));
            for (const auto& [n, amount] : k.advertise)
                v.needs[size_t(n)] =
                    std::clamp(v.needs[size_t(n)] + amount * m / k.minutes, 0.0f, 100.0f);
            v.use_left -= float(minutes);
            v.last_progress = minute_;
        } else if (v.state == Villager::State::Walking) {
            walk(v, float(seconds));
        }
        // A need below critical: one message each time it falls below.
        for (size_t n = 0; n < kit_.needs.size(); ++n) {
            const Need& need = kit_.needs[n];
            if (!v.below[n] && v.needs[n] < need.critical) {
                v.below[n] = true;
                if (v.thing >= 0) {
                    if (auto h = v.handle(NeedCritical{moira::Symbol(need.name)}); !h) {
                        ++tally_.script_errors;
                        note(v,
                             "on_need_critical failed: " + std::string(moira::describe(h.error())));
                    }
                }
            } else if (v.below[n] && v.needs[n] >= need.critical + 5) {
                v.below[n] = false;
            }
        }
        if (v.state == Villager::State::Idle) {
            v.last_progress = minute_;
            if (minute_ >= v.idle_until) decide(v, "decided");
        } else if (minute_ - v.last_progress > double(r.stuck_minutes)) {
            ++tally_.stuck;
            if (tally_.notes.size() < kNotes)
                tally_.notes.push_back(clock_text(minute_) + " " + house().name + " " + v.name +
                                       " stuck in " + std::string(v.action.str()));
            note(v, "stuck: gives up " + std::string(v.action.str()));
            v.interrupted.clear();
            end_action(v, true);
        }
    }
    const int hour = int(minute_ / 60);
    if (hour != last_hour_) {
        last_hour_ = hour;
        for (auto& v : villagers_) record_hour(*v);
    }
    if (tick) clock_.tick(minutes);
    ++tally_.steps;
}

Tally Household::tally() const {
    Tally t = tally_;
    t.character_days = double(villagers_.size()) * minute_ / 1440;
    int used = 0;
    for (const auto& th : things_) used += th->uses > 0;
    t.things_used = used;
    for (const auto& per_hour : hours_) {
        double sum = 0;
        int hours = 0;
        for (const auto& counts : per_hour) {
            int total = 0;
            for (const auto& [a, n] : counts) total += n;
            if (total == 0) continue;
            double h = 0;
            for (const auto& [a, n] : counts) {
                const double p = double(n) / double(total);
                h -= p * std::log2(p);
            }
            sum += h;
            ++hours;
        }
        t.entropy.push_back(hours ? sum / hours : 0);
    }
    return t;
}

std::string Household::why(const Villager& v) const {
    std::string s = v.name + ", " + clock_text(v.last.minute) + ", " + v.last.why + ":";
    for (size_t i = 0; i < v.last.options.size() && i < 6; ++i) {
        const Decision::Option& o = v.last.options[i];
        char buf[96];
        std::snprintf(
            buf, sizeof buf, "%s %s %.3f", i == size_t(v.last.chosen) ? "  >" : "   ",
            o.thing < 0 ? "idle" : kit_.kinds[size_t(things_[size_t(o.thing)]->kind)].name.c_str(),
            double(o.score));
        s += buf;
    }
    if (v.last.options.size() > 1) {
        char buf[64];
        std::snprintf(
            buf, sizeof buf, "   margin %.0f%%",
            100.0 * double(1 - v.last.options[1].score / std::max(1e-6f, v.last.options[0].score)));
        s += buf;
    }
    return s;
}

Villager* Household::villager(const std::string& name) {
    for (auto& v : villagers_)
        if (v->name == name) return v.get();
    return nullptr;
}

void prepare(moira::Context& ctx, moira::Scheduler& clock, const Kit& kit,
             std::vector<std::string>* errors) {
    ctx.add_type<Villager>();
    ctx.add_type<Thing>();
    if (ctx.object(moira::Symbol("clock")).type() != moira::Type::Object)
        (void)ctx.add(clock, moira::Symbol("clock"));
    for (const moira::Node& n : kit.setup)
        if (auto r = ctx.eval(n); !r && errors)
            errors->push_back(std::string(moira::describe(r.error())));
}

SoakResult soak(const Kit& kit, const SoakOptions& o) {
    SoakResult result;
    result.households = std::max(0, o.households);
    int threads = o.threads > 0
                      ? o.threads
                      : int(std::min(8u, std::max(1u, std::thread::hardware_concurrency())));
    threads = std::max(1, std::min(threads, std::max(1, result.households)));
    result.threads = threads;
    std::vector<Tally> tallies(size_t(result.households));
    std::vector<std::vector<std::string>> errors(static_cast<size_t>(threads));
    const auto start = std::chrono::steady_clock::now();
    auto work = [&](int w) {
        moira::Context ctx;
        moira::Scheduler clock(ctx);
        prepare(ctx, clock, kit, &errors[size_t(w)]);
        const long steps = long(o.days * 86400 / o.step_seconds);
        for (int i = w; i < result.households; i += threads) {
            Household h(
                kit, i % int(kit.houses.size()), ctx, clock,
                base::Rng(o.seed + (uint64_t(i) + 1) * 0x632BE59BD9B4E019ull, 0x5851F42D4C957F2Dull)
                    .state);
            for (long s = 0; s < steps; ++s) h.step(o.step_seconds);
            tallies[size_t(i)] = h.tally();
        }
    };
    std::vector<std::thread> pool;
    for (int w = 1; w < threads; ++w) pool.emplace_back(work, w);
    work(0);
    for (std::thread& t : pool) t.join();
    result.wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (const Tally& t : tallies) result.tally.add(t);
    result.errors = std::move(errors[0]);
    return result;
}

double bench(const Kit& kit, int characters, double speed) {
    moira::Context ctx;
    moira::Scheduler clock(ctx);
    prepare(ctx, clock, kit);
    std::vector<std::unique_ptr<Household>> homes;
    int have = 0;
    for (int i = 0; have < characters && i < 10000; ++i) {
        const int house = i % int(kit.houses.size());
        if (kit.houses[size_t(house)].residents.empty()) continue;
        homes.push_back(std::make_unique<Household>(kit, house, ctx, clock, uint64_t(i) + 1));
        have += int(kit.houses[size_t(house)].residents.size());
    }
    const double dt = speed / 60.0;
    auto step = [&] {
        for (auto& h : homes) h->step(dt, false);
        clock.tick(dt / 60);
    };
    for (int s = 0; s < 600; ++s) step();
    const int steps = 3600;
    const auto start = std::chrono::steady_clock::now();
    for (int s = 0; s < steps; ++s) step();
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    return have > 0 ? us / steps * double(characters) / double(have) : 0;
}

}  // namespace hearth
