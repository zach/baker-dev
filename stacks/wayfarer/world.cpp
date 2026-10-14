#include "world.hpp"

#include <baker/std_interop.hpp>
#include <base/rng.hpp>
#include <base/words.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace wayfarer {
namespace {

constexpr size_t kMaxBits = 64;
constexpr size_t kSoftlocksKept = 8;

float num(const baker::Array& a, size_t i, float fallback = 0) {
    return i < a.size() ? static_cast<float>(a[i].as_float().value_or(fallback)) : fallback;
}
bool point(const baker::Node& n, base::NavPoint& out) {
    const baker::Array* a = n.try_array();
    if (!a || a->size() < 3) return false;
    out = {num(*a, 0), num(*a, 1), num(*a, 2)};
    return true;
}

uint64_t mix(uint64_t h, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    for (int i = 0; i < 4; ++i) h = (h ^ ((bits >> (8 * i)) & 0xff)) * 0x100000001B3ull;
    return h;
}

float dist(const base::NavPoint& a, const base::NavPoint& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}
float flat(const base::NavPoint& a, const base::NavPoint& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

bool read_goal(const baker::Node& n, const World& w, Goal& g, std::string& error) {
    g.command = n;
    g.fact = Fact::Other;
    const baker::Array* a = n.type() == baker::Type::Command ? n.array() : nullptr;
    if (!a || a->size() != 2) return true;
    const auto head = (*a)[0].try_sym();
    const auto arg = (*a)[1].try_sym();
    if (!head || !arg) return true;
    const std::string h(head->str()), name(arg->str());
    auto find = [&](int index, Fact f, const char* what) {
        if (index < 0) {
            error = std::string("{") + h + " " + name + "}: no " + what + " " + name;
            return false;
        }
        g.fact = f;
        g.target = index;
        return true;
    };
    if (h == "at_poi") return find(w.poi(name), Fact::AtPoi, "point of interest");
    if (h == "flag") return find(w.flag(name), Fact::Flag, "flag (nothing sets it)");
    if (h == "has_item") return find(w.item(name), Fact::HasItem, "item");
    if (h == "mission.done") return find(w.mission(name), Fact::MissionDone, "mission");
    return true;
}

// Reads `{and a b ...}` as several goals, anything else as one.
bool read_goals(const baker::Node& n, const World& w, std::vector<Goal>& out, std::string& error) {
    const baker::Array* a = n.type() == baker::Type::Command ? n.array() : nullptr;
    if (a && !a->empty() && (*a)[0].try_sym() && (*a)[0].try_sym()->str() == "and") {
        for (size_t i = 1; i < a->size(); ++i)
            if (!read_goals((*a)[i], w, out, error)) return false;
        return true;
    }
    Goal g;
    if (!read_goal(n, w, g, error)) return false;
    out.push_back(std::move(g));
    return true;
}

}  // namespace

int World::poi(const std::string& name) const {
    for (size_t i = 0; i < pois.size(); ++i)
        if (pois[i].name == name) return int(i);
    return -1;
}
int World::item(const std::string& name) const {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i] == name) return int(i);
    return -1;
}
int World::flag(const std::string& name) const {
    for (size_t i = 0; i < flags.size(); ++i)
        if (flags[i] == name) return int(i);
    return -1;
}
int World::mission(const std::string& name) const {
    for (size_t i = 0; i < missions.size(); ++i)
        if (missions[i].name == name) return int(i);
    return -1;
}
int World::cell_at(float x, float z) const {
    const int cx = int(std::floor(x / cell_size)), cz = int(std::floor(z / cell_size));
    for (size_t i = 0; i < cells.size(); ++i)
        if (cells[i].cx == cx && cells[i].cz == cz) return int(i);
    return -1;
}
uint16_t World::abilities_of(uint64_t held) const {
    uint16_t f = base::nav::kWalk;
    for (const Ability& a : abilities)
        if (a.item < 0 || (held >> a.item & 1)) f |= a.flag;
    return f;
}

bool load(const baker::Ref<baker::Array>& root, World& out, std::string& error) {
    World w;
    w.tree = root;
    if (const baker::Array* r = root->try_find(baker::Symbol("wayfarer"))) {
        Traversal& t = w.rules;
        t.run_speed = base::number(*r, "run_speed", t.run_speed);
        t.climb_max = base::number(*r, "climb_max", t.climb_max);
        t.climb_speed = base::number(*r, "climb_speed", t.climb_speed);
        t.glide_speed = base::number(*r, "glide_speed", t.glide_speed);
        t.glide_sink = std::max(0.1f, base::number(*r, "glide_sink", t.glide_sink));
        t.step = base::number(*r, "step", t.step);
        t.poi_radius = base::number(*r, "poi_radius", t.poi_radius);
        t.discover_radius = base::number(*r, "discover_radius", t.discover_radius);
    }
    if (const baker::Array* a = root->try_find(baker::Symbol("world"))) {
        w.cell_size = std::max(8.0f, base::number(*a, "cell_size", w.cell_size));
        if (const baker::Array* s = a->try_find(baker::Symbol("spawn")); s && s->size() > 3)
            w.spawn = {num(*s, 1), num(*s, 2), num(*s, 3)};
    }
    if (const baker::Array* a = root->try_find(baker::Symbol("items")))
        for (size_t i = 1; i < a->size(); ++i)
            if (auto s = (*a)[i].try_sym()) w.items.emplace_back(s->str());
    if (w.items.size() > kMaxBits) {
        error = "more than 64 items";
        return false;
    }
    if (const baker::Array* a = root->try_find(baker::Symbol("abilities")))
        for (const baker::Array& ab : a->subarrays()) {
            Ability x;
            x.name = base::name(ab);
            x.flag = x.name == "climb"   ? base::nav::kClimb
                     : x.name == "glide" ? base::nav::kGlide
                     : x.name == "swim"  ? base::nav::kSwim
                                         : uint16_t(0);
            if (!x.flag) {
                error = "ability " + x.name + ": the abilities are climb, glide, and swim";
                return false;
            }
            if (auto it = ab.find_sym(baker::Symbol("item"), baker::Symbol());
                it && !it->str().empty()) {
                x.item = w.item(std::string(it->str()));
                if (x.item < 0) {
                    error = "ability " + x.name + " names item " + std::string(it->str()) +
                            ", which is not one of (items ...)";
                    return false;
                }
            }
            w.abilities.push_back(std::move(x));
        }
    if (const baker::Array* a = root->try_find(baker::Symbol("cells")))
        for (const baker::Array& ca : a->subarrays()) {
            Cell c;
            c.name = base::name(ca);
            if (const baker::Array* at = ca.try_find(baker::Symbol("grid"))) {
                c.cx = int(num(*at, 1));
                c.cz = int(num(*at, 2));
            }
            uint64_t h = 0xCBF29CE484222325ull;
            for (const char* tag : {"blocks", "climbable"})
                if (const baker::Array* bs = ca.try_find(baker::Symbol(tag)))
                    for (size_t i = 1; i < bs->size(); ++i) {
                        const baker::Array* b = (*bs)[i].try_array();
                        if (!b || b->size() < 5) {
                            error = "cell " + c.name + ": a block is (x z width depth height [y])";
                            return false;
                        }
                        Block k{num(*b, 0),
                                num(*b, 1),
                                num(*b, 2),
                                num(*b, 3),
                                num(*b, 4),
                                num(*b, 5),
                                std::strcmp(tag, "climbable") == 0};
                        for (float v : {k.x, k.z, k.w, k.d, k.h, k.y, k.climb ? 1.0f : 0.0f})
                            h = mix(h, v);
                        c.blocks.push_back(k);
                    }
            for (const char* tag : {"drops", "glides"})
                if (const baker::Array* ms = ca.try_find(baker::Symbol(tag)))
                    for (size_t i = 1; i < ms->size(); ++i) {
                        const baker::Array* m = (*ms)[i].try_array();
                        Move mv;
                        mv.glide = std::strcmp(tag, "glides") == 0;
                        if (!m || m->size() < 2 || !point((*m)[0], mv.from) ||
                            !point((*m)[1], mv.to)) {
                            error = "cell " + c.name + ": a " + tag + " entry is ((x y z) (x y z))";
                            return false;
                        }
                        for (float v : {mv.from.x, mv.from.y, mv.from.z, mv.to.x, mv.to.y, mv.to.z})
                            h = mix(h, v);
                        c.moves.push_back(mv);
                    }
            c.version = h;
            w.cells_x = std::max(w.cells_x, c.cx + 1);
            w.cells_z = std::max(w.cells_z, c.cz + 1);
            w.cells.push_back(std::move(c));
        }
    if (w.cells.empty()) {
        error = "no (cells ...) in the data";
        return false;
    }
    if (const baker::Array* a = root->try_find(baker::Symbol("pois")))
        for (const baker::Array& pa : a->subarrays()) {
            Poi p;
            p.name = base::name(pa);
            if (const baker::Array* at = pa.try_find(baker::Symbol("pos")))
                p.at = {num(*at, 1), num(*at, 2), num(*at, 3)};
            for (const baker::Array& f : pa.subarrays()) {
                const std::string t = base::name(f);
                if (t == "landmark") p.landmark = true;
                if (t == "gives" || t == "needs")
                    for (size_t i = 1; i < f.size(); ++i) {
                        const auto s = f[i].try_sym();
                        const int it = s ? w.item(std::string(s->str())) : -1;
                        if (it < 0) {
                            error =
                                "poi " + p.name + ": " + t + " names an item not in (items ...)";
                            return false;
                        }
                        (t == "gives" ? p.gives : p.needs).push_back(it);
                    }
                if (t == "sets" && f.size() > 1) {
                    const auto s = f[1].try_sym();
                    const std::string name = s ? std::string(s->str()) : std::string();
                    int fl = w.flag(name);
                    if (fl < 0) {
                        fl = int(w.flags.size());
                        w.flags.push_back(name);
                    }
                    p.sets = fl;
                }
            }
            w.pois.push_back(std::move(p));
        }
    if (w.flags.size() > kMaxBits) {
        error = "more than 64 flags";
        return false;
    }
    // Missions come one (missions ...) per region file: read them all, names first, so a
    // mission can unlock on one defined after it.
    std::vector<std::pair<const baker::Array*, std::string>> defs;
    for (const baker::Node& n : root->nodes()) {
        const baker::Array* a = n.try_array();
        if (!a || base::name(*a) != "missions") continue;
        for (const baker::Array& ma : a->subarrays()) {
            Mission m;
            m.name = base::name(ma);
            if (auto r = ma.find_sym(baker::Symbol("region"), baker::Symbol()); r)
                m.region = std::string(r->str());
            m.file = std::string(ma.file().str());
            w.missions.push_back(std::move(m));
            defs.emplace_back(&ma, w.missions.back().name);
        }
    }
    if (w.missions.size() > kMaxBits) {
        error = "more than 64 missions";
        return false;
    }
    for (size_t mi = 0; mi < defs.size(); ++mi) {
        const baker::Array& ma = *defs[mi].first;
        Mission& m = w.missions[mi];
        if (const baker::Array* u = ma.try_find(baker::Symbol("unlock")))
            for (size_t i = 1; i < u->size(); ++i)
                if (!read_goals((*u)[i], w, m.unlock, error)) {
                    error = "mission " + m.name + " unlock: " + error;
                    return false;
                }
        if (const baker::Array* os = ma.try_find(baker::Symbol("objectives")))
            for (const baker::Array& oa : os->subarrays()) {
                Objective o;
                o.name = base::name(oa);
                o.hint_after = base::number(oa, "hint_after", -1);
                const baker::Array* g = oa.try_find(baker::Symbol("goal"));
                if (!g || g->size() < 2 || !read_goal((*g)[1], w, o.goal, error)) {
                    error = "mission " + m.name + " objective " + o.name + ": " +
                            (error.empty() ? std::string("no (goal ...)") : error);
                    return false;
                }
                m.objectives.push_back(std::move(o));
            }
        if (m.objectives.empty() || m.objectives.size() > kMaxBits) {
            error = "mission " + m.name + " has no (objectives ...), or more than 64";
            return false;
        }
        // Requires name objectives of the same mission.
        size_t oi = 0;
        if (const baker::Array* os = ma.try_find(baker::Symbol("objectives")))
            for (const baker::Array& oa : os->subarrays()) {
                if (const baker::Array* rq = oa.try_find(baker::Symbol("requires")))
                    for (size_t i = 1; i < rq->size(); ++i) {
                        const auto s = (*rq)[i].try_sym();
                        int found = -1;
                        for (size_t k = 0; k < m.objectives.size(); ++k)
                            if (s && m.objectives[k].name == s->str()) found = int(k);
                        if (found < 0) {
                            error = "mission " + m.name + " objective " + m.objectives[oi].name +
                                    " requires an objective it does not have";
                            return false;
                        }
                        m.objectives[oi].after.push_back(found);
                    }
                ++oi;
            }
        if (const baker::Array* rw = ma.try_find(baker::Symbol("reward"))) {
            m.xp = int(base::number(*rw, "xp", 0));
            for (const baker::Array& r : rw->subarrays())
                if (base::name(r) == "item")
                    for (size_t i = 1; i < r.size(); ++i) {
                        const auto s = r[i].try_sym();
                        const int it = s ? w.item(std::string(s->str())) : -1;
                        if (it < 0) {
                            error = "mission " + m.name + " rewards an item not in (items ...)";
                            return false;
                        }
                        m.reward_items.push_back(it);
                    }
        }
        m.par = base::number(ma, "par", -1);
    }
    out = std::move(w);
    return true;
}

// --- Navigation ---

base::NavSettings nav_settings(const World& w) {
    base::NavSettings s;
    s.tile_size = w.cell_size;
    s.cell_size = 0.3f;
    s.cell_height = 0.2f;
    s.agent_radius = 0.4f;
    s.agent_height = 1.8f;
    s.max_climb = w.rules.step;
    s.bottom = -5;
    s.top = 80;
    s.max_tiles = std::max(1, w.cells_x * w.cells_z);
    return s;
}

base::NavGeometry geometry(const World& w, int c, int* skipped) {
    base::NavGeometry g;
    const Cell& cell = w.cells[size_t(c)];
    for (const Cell& o : w.cells) {
        if (std::abs(o.cx - cell.cx) > 1 || std::abs(o.cz - cell.cz) > 1) continue;
        const float half = w.cell_size / 2;
        g.box({float(o.cx) * w.cell_size + half, -0.25f, float(o.cz) * w.cell_size + half},
              {half, 0.25f, half});
        for (const Block& b : o.blocks) {
            g.box({b.x, b.y + b.h / 2, b.z}, {b.w / 2, b.h / 2, b.d / 2});
            if (!b.climb || b.h > w.rules.climb_max) continue;
            // A climb up each face, and back down: from the foot, a metre out, to the top, a
            // metre in (a ledge to climb from or onto must be about two metres deep).
            const float out = 1.0f;
            const base::NavPoint faces[4][2] = {
                {{b.x - b.w / 2 - out, b.y, b.z}, {b.x - b.w / 2 + out, b.y + b.h, b.z}},
                {{b.x + b.w / 2 + out, b.y, b.z}, {b.x + b.w / 2 - out, b.y + b.h, b.z}},
                {{b.x, b.y, b.z - b.d / 2 - out}, {b.x, b.y + b.h, b.z - b.d / 2 + out}},
                {{b.x, b.y, b.z + b.d / 2 + out}, {b.x, b.y + b.h, b.z + b.d / 2 - out}}};
            for (const auto& f : faces) g.link(f[0], f[1], base::nav::kClimb, true);
        }
        for (const Move& m : o.moves) {
            if (m.glide) {
                const float drop = m.from.y - m.to.y;
                if (drop <= 0 || flat(m.from, m.to) > drop * w.rules.glide_ratio()) {
                    if (skipped && &o == &cell) ++*skipped;
                    continue;
                }
            }
            g.link(m.from, m.to, m.glide ? base::nav::kGlide : base::nav::kWalk, false);
        }
    }
    return g;
}

uint64_t nav_key(const World& w) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (const Cell& c : w.cells) {
        h = mix(h, float(c.cx));
        h = mix(h, float(c.cz));
        for (int i = 0; i < 8; ++i) h = (h ^ ((c.version >> (8 * i)) & 0xff)) * 0x100000001B3ull;
    }
    for (float v : {w.cell_size, w.rules.climb_max, w.rules.step, w.rules.glide_ratio()})
        h = mix(h, v);
    return h;
}

bool build_nav(const World& w, base::NavMesh& nav, std::string* error) {
    if (!nav.init(nav_settings(w))) {
        if (error) *error = "nav: could not initialize";
        return false;
    }
    for (size_t c = 0; c < w.cells.size(); ++c)
        if (!nav.add_tile(w.cells[c].cx, w.cells[c].cz, geometry(w, int(c)), error)) return false;
    return true;
}

std::vector<float> glide_margins(const World& w) {
    std::vector<float> out;
    for (const Cell& c : w.cells)
        for (const Move& m : c.moves)
            if (m.glide) {
                const float drop = m.from.y - m.to.y;
                const float need = drop > 0 ? flat(m.from, m.to) / drop : 1e9f;
                out.push_back(w.rules.glide_ratio() / need);
            }
    return out;
}

// --- Progress ---

void Progress::reset(const World& w) {
    items = flags = missions = started = 0;
    objectives.assign(w.missions.size(), 0);
    found.assign(w.pois.size(), false);
}

bool holds(const World& w, const Progress& p, const Goal& g, int place) {
    (void)w;
    switch (g.fact) {
        case Fact::AtPoi: return place == g.target;
        case Fact::Flag: return (p.flags >> g.target) & 1;
        case Fact::HasItem: return p.has(g.target);
        case Fact::MissionDone: return p.done(g.target);
        case Fact::Other: return false;
    }
    return false;
}

bool Progress::unlocked(const World& w, int m) const {
    if (started >> m & 1) return true;
    for (const Goal& g : w.missions[size_t(m)].unlock)
        if (!holds(w, *this, g, -2)) return false;
    return true;
}

bool Progress::open(const World& w, int m, int o) const {
    if (done(m) || !unlocked(w, m)) return false;
    const uint64_t bits = objectives[size_t(m)];
    if (bits >> o & 1) return false;
    for (int r : w.missions[size_t(m)].objectives[size_t(o)].after)
        if (!(bits >> r & 1)) return false;
    return true;
}

int settle(const World& w, Progress& p, int place) {
    int done = 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t m = 0; m < w.missions.size(); ++m) {
            const Mission& mi = w.missions[m];
            for (size_t o = 0; o < mi.objectives.size(); ++o)
                if (p.open(w, int(m), int(o)) && holds(w, p, mi.objectives[o].goal, place)) {
                    p.objectives[m] |= uint64_t(1) << o;
                    ++done;
                    changed = true;
                }
            const uint64_t all = mi.objectives.size() >= 64
                                     ? ~uint64_t(0)
                                     : (uint64_t(1) << mi.objectives.size()) - 1;
            if (!p.done(int(m)) && p.objectives[m] == all) {
                p.missions |= uint64_t(1) << m;
                for (int it : mi.reward_items) p.items |= uint64_t(1) << it;
                changed = true;
            }
        }
    }
    return done;
}

int visit(const World& w, Progress& p, int at) {
    take(w, p, at);
    return settle(w, p, at);
}

void take(const World& w, Progress& p, int at) {
    const Poi& poi = w.pois[size_t(at)];
    p.found[size_t(at)] = true;
    bool ready = true;
    for (int it : poi.needs) ready &= p.has(it);
    if (ready) {
        for (int it : poi.gives) p.items |= uint64_t(1) << it;
        if (poi.sets >= 0) p.flags |= uint64_t(1) << poi.sets;
    }
}

// --- Proof ---

namespace {

// Paths between places (the spawn, then each poi) for each set of abilities, computed once.
class Reach {
public:
    Reach(const World& w, const base::NavMesh& nav) : w_(w), nav_(nav) {
        places_.push_back(w.spawn);
        for (const Poi& p : w.pois) places_.push_back(p.at);
        on_mesh_.resize(places_.size());
        for (size_t i = 0; i < places_.size(); ++i) {
            base::NavPoint at;
            on_mesh_[i] = nav.nearest(places_[i], at, 2.5f, base::nav::kAll);
            if (on_mesh_[i]) places_[i] = at;
        }
    }
    [[nodiscard]] bool can(size_t from, size_t to, uint16_t abilities) {
        if (from == to) return true;
        if (!on_mesh_[from] || !on_mesh_[to]) return false;
        const uint64_t key = (uint64_t(from) << 40) | (uint64_t(to) << 16) | abilities;
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        std::vector<base::NavPoint> path;
        bool ok = nav_.path(places_[from], places_[to], path, abilities);
        // Within the poi's radius of it counts as there.
        if (!ok && !path.empty()) ok = dist(path.back(), places_[to]) <= w_.rules.poi_radius;
        cache_.emplace(key, ok);
        return ok;
    }
    [[nodiscard]] size_t size() const { return places_.size(); }

private:
    const World& w_;
    const base::NavMesh& nav_;
    std::vector<base::NavPoint> places_;
    std::vector<bool> on_mesh_;
    std::map<uint64_t, bool> cache_;
};

struct State {
    int place = -1;  // a poi, or -1 the spawn
    Progress p;
};

std::vector<uint64_t> key_of(const State& s) {
    std::vector<uint64_t> k{uint64_t(int64_t(s.place)), s.p.items, s.p.flags, s.p.missions};
    k.insert(k.end(), s.p.objectives.begin(), s.p.objectives.end());
    return k;
}

std::string names(const std::vector<std::string>& all, uint64_t bits) {
    std::string s;
    for (size_t i = 0; i < all.size(); ++i)
        if (bits >> i & 1) s += (s.empty() ? "" : " ") + all[i];
    return s.empty() ? "nothing" : s;
}

}  // namespace

Proof prove(const World& w, const base::NavMesh& nav) {
    Proof proof;
    for (const Mission& m : w.missions) {
        for (const Goal& g : m.unlock)
            if (g.fact == Fact::Other) proof.unanalysed.push_back(m.name + " unlock");
        for (const Objective& o : m.objectives)
            if (o.goal.fact == Fact::Other) proof.unanalysed.push_back(m.name + "." + o.name);
    }
    Reach reach(w, nav);
    std::vector<State> states;
    std::map<std::vector<uint64_t>, int> ids;
    std::vector<std::vector<int>> edges;
    auto add = [&](State s) {
        auto k = key_of(s);
        auto [it, fresh] = ids.emplace(std::move(k), int(states.size()));
        if (fresh) {
            states.push_back(std::move(s));
            edges.emplace_back();
        }
        return it->second;
    };
    State start;
    start.p.reset(w);
    settle(w, start.p, -1);
    add(start);
    for (size_t i = 0; i < states.size(); ++i) {
        const uint16_t abilities = w.abilities_of(states[i].p.items);
        const size_t from = size_t(states[i].place + 1);
        for (size_t to = 1; to < reach.size(); ++to) {
            if (!reach.can(from, to, abilities)) continue;
            State next = states[i];
            next.place = int(to) - 1;
            visit(w, next.p, next.place);
            // Discovery is not progress: a save point is where you are and what you have.
            std::fill(next.p.found.begin(), next.p.found.end(), false);
            const int j = add(std::move(next));
            if (j != int(i)) edges[i].push_back(j);
        }
    }
    proof.states = int(states.size());
    const uint64_t all =
        w.missions.size() >= 64 ? ~uint64_t(0) : (uint64_t(1) << w.missions.size()) - 1;
    // Which states can still finish everything: backwards from the finished ones.
    std::vector<std::vector<int>> back(states.size());
    for (size_t i = 0; i < edges.size(); ++i)
        for (int j : edges[i]) back[size_t(j)].push_back(int(i));
    std::vector<bool> can(states.size(), false);
    std::vector<int> queue;
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].p.missions == all) can[i] = true, queue.push_back(int(i));
    while (!queue.empty()) {
        const int j = queue.back();
        queue.pop_back();
        for (int i : back[size_t(j)])
            if (!can[size_t(i)]) can[size_t(i)] = true, queue.push_back(i);
    }
    proof.complete = !states.empty() && can[0];
    for (size_t i = 0; i < states.size(); ++i) {
        if (can[i]) continue;
        ++proof.softlocked_states;
        if (proof.softlocks.size() >= kSoftlocksKept) continue;
        const State& s = states[i];
        std::vector<std::string> mission_names;
        for (const Mission& m : w.missions) mission_names.push_back(m.name);
        Softlock l;
        l.where = std::string("at ") + (s.place < 0 ? "the spawn" : w.pois[size_t(s.place)].name) +
                  ", holding " + names(w.items, s.p.items) + ", done " +
                  names(mission_names, s.p.missions);
        l.why = "cannot finish " + names(mission_names, all & ~s.p.missions);
        proof.softlocks.push_back(std::move(l));
    }
    std::vector<bool> been(w.pois.size(), false);
    for (const State& s : states)
        if (s.place >= 0) been[size_t(s.place)] = true;
    for (size_t i = 0; i < w.pois.size(); ++i)
        if (!been[i]) proof.unreachable_pois.push_back(w.pois[i].name);
    for (size_t m = 0; m < w.missions.size(); ++m)
        for (size_t o = 0; o < w.missions[m].objectives.size(); ++o) {
            bool ever = false;
            for (const State& s : states) ever |= (s.p.objectives[m] >> o & 1) != 0;
            if (!ever)
                proof.unreachable.push_back(w.missions[m].name + "." +
                                            w.missions[m].objectives[o].name);
        }
    return proof;
}

// --- Pacing ---

bool parse_style(const std::string& s, Style& out) {
    if (s == "direct")
        out = Style::Direct;
    else if (s == "curious")
        out = Style::Curious;
    else if (s == "completionist")
        out = Style::Completionist;
    else
        return false;
    return true;
}

Pacing explore(const World& w, const base::NavMesh& nav, Style style, float seconds,
               uint64_t seed) {
    Pacing out;
    out.mission_done.assign(w.missions.size(), -1);
    base::Rng rng(seed, 0x5851F42D4C957F2Dull);
    Progress p;
    p.reset(w);
    base::NavPoint pos = w.spawn;
    if (base::NavPoint at; nav.nearest(pos, at, 3, base::nav::kAll)) pos = at;
    float t = 0;
    const Traversal& r = w.rules;
    auto discover = [&](const base::NavPoint& at) {
        for (size_t i = 0; i < w.pois.size(); ++i)
            if (!p.found[i] && dist(at, w.pois[i].at) <= r.discover_radius) {
                p.found[i] = true;
                out.discoveries.push_back(t);
            }
    };
    auto note_missions = [&] {
        for (size_t m = 0; m < w.missions.size(); ++m)
            if (p.done(int(m)) && out.mission_done[m] < 0) out.mission_done[m] = t;
    };
    settle(w, p, -1);
    discover(pos);
    std::vector<base::NavPoint> path;
    auto path_to = [&](int poi, std::vector<base::NavPoint>& out_path) {
        const uint16_t ab = w.abilities_of(p.items);
        if (nav.path(pos, w.pois[size_t(poi)].at, out_path, ab)) return true;
        return !out_path.empty() && dist(out_path.back(), w.pois[size_t(poi)].at) <= r.poi_radius;
    };
    auto length = [](const std::vector<base::NavPoint>& pth) {
        float d = 0;
        for (size_t i = 1; i < pth.size(); ++i) d += dist(pth[i - 1], pth[i]);
        return d;
    };
    // The poi that makes progress on an open objective, if one can be walked to.
    auto direct_target = [&]() -> int {
        for (size_t m = 0; m < w.missions.size(); ++m)
            for (size_t o = 0; o < w.missions[m].objectives.size(); ++o) {
                if (!p.open(w, int(m), int(o))) continue;
                const Goal& g = w.missions[m].objectives[o].goal;
                std::vector<int> candidates;
                for (size_t i = 0; i < w.pois.size(); ++i) {
                    const Poi& q = w.pois[i];
                    bool ready = true;
                    for (int it : q.needs) ready &= p.has(it);
                    if (g.fact == Fact::AtPoi && int(i) == g.target) candidates.push_back(int(i));
                    if (g.fact == Fact::Flag && q.sets == g.target) {
                        if (ready) candidates.push_back(int(i));
                        // Otherwise, what it needs.
                        for (int it : q.needs)
                            if (!p.has(it))
                                for (size_t k = 0; k < w.pois.size(); ++k)
                                    for (int give : w.pois[k].gives)
                                        if (give == it) candidates.push_back(int(k));
                    }
                    if (g.fact == Fact::HasItem)
                        for (int give : q.gives)
                            if (give == g.target && ready) candidates.push_back(int(i));
                }
                for (int c : candidates)
                    if (path_to(c, path)) return c;
            }
        // Nothing open can be reached: go for an item not yet held (a tool opens the way).
        for (size_t i = 0; i < w.pois.size(); ++i) {
            bool ready = true, gives_new = false;
            for (int it : w.pois[i].needs) ready &= p.has(it);
            for (int it : w.pois[i].gives) gives_new |= !p.has(it);
            if (ready && gives_new && path_to(int(i), path)) return int(i);
        }
        return -1;
    };
    auto nearest_unfound = [&](bool in_sight) -> int {
        int best = -1;
        float best_d = 1e30f;
        for (size_t i = 0; i < w.pois.size(); ++i) {
            if (p.found[i]) continue;
            const float straight = flat(pos, w.pois[i].at);
            if (in_sight && straight > 3 * r.discover_radius) continue;
            std::vector<base::NavPoint> pth;
            if (!path_to(int(i), pth)) continue;
            const float d = in_sight ? straight : length(pth);
            if (d < best_d) best_d = d, best = int(i);
        }
        return best;
    };
    int last_target = -1, repeats = 0;
    while (t < seconds) {
        const bool all_missions =
            w.missions.empty() ||
            p.missions ==
                ((w.missions.size() >= 64) ? ~uint64_t(0) : (uint64_t(1) << w.missions.size()) - 1);
        bool all_found = true;
        for (bool f : p.found) all_found &= f;
        if (style == Style::Direct && all_missions) break;
        if (style == Style::Completionist && all_missions && all_found) break;
        if (style == Style::Curious && all_found) break;
        int target = -1;
        switch (style) {
            case Style::Direct:
                target = direct_target();
                if (target < 0) target = nearest_unfound(false);
                break;
            case Style::Curious:
                target = nearest_unfound(true);
                if (target < 0) target = direct_target();
                if (target < 0) {
                    // Wander: a point it has not found, somewhere it can walk to.
                    std::vector<int> can;
                    for (size_t i = 0; i < w.pois.size(); ++i) {
                        std::vector<base::NavPoint> pth;
                        if (!p.found[i] && path_to(int(i), pth)) can.push_back(int(i));
                    }
                    if (!can.empty())
                        target = can[size_t(static_cast<float>(rng.unit()) * float(can.size())) %
                                     can.size()];
                }
                break;
            case Style::Completionist:
                target = nearest_unfound(false);
                if (target < 0) target = direct_target();
                break;
        }
        if (target < 0 || !path_to(target, path)) break;  // nothing left it can reach
        repeats = target == last_target ? repeats + 1 : 0;
        last_target = target;
        if (repeats > 3) break;  // going back and forth to no effect
        // Walk the path, one second at a time, finding what comes in sight.
        for (size_t i = 1; i < path.size() && t < seconds; ++i) {
            const base::NavPoint a = path[i - 1], b = path[i];
            const float rise = b.y - a.y, run = flat(a, b);
            float time = dist(a, b) / r.run_speed;
            if (rise > r.step && run < 2)
                time = rise / r.climb_speed;  // a climb
            else if (-rise > r.step && run > 2)
                time = std::max(time, run / r.glide_speed);  // a glide
            const int ticks = std::max(1, int(time));
            for (int k = 1; k <= ticks && t < seconds; ++k) {
                t += time / float(ticks);
                const float f = float(k) / float(ticks);
                discover({a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f});
            }
            pos = b;
        }
        if (t >= seconds) break;
        pos = w.pois[size_t(target)].at;
        visit(w, p, target);
        note_missions();
    }
    note_missions();
    // Once every point is found there is nothing left to find: no dead zone after that.
    bool everything = true;
    for (bool f : p.found) everything &= f;
    float end = std::min(t, seconds);
    if (everything && !out.discoveries.empty())
        end = *std::max_element(out.discoveries.begin(), out.discoveries.end());
    std::sort(out.discoveries.begin(), out.discoveries.end());
    float prev = 0;
    for (float d : out.discoveries) {
        if (d - prev > out.longest_gap) out.longest_gap = d - prev, out.longest_gap_at = prev;
        prev = d;
    }
    if (end - prev > out.longest_gap) out.longest_gap = end - prev, out.longest_gap_at = prev;
    out.found = int(out.discoveries.size());
    for (size_t i = 0; i < w.pois.size(); ++i)
        if (!p.found[i]) out.missed.push_back(int(i));
    bool all = true;
    float last = 0;
    for (float d : out.mission_done) {
        all &= d >= 0;
        last = std::max(last, d);
    }
    out.all_done = all ? last : -1;
    return out;
}

}  // namespace wayfarer
