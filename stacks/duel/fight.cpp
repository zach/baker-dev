#include "fight.hpp"

#include <baker/std_interop.hpp>
#include <base/rng.hpp>
#include <base/words.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace duel {
namespace {

// Metres to millimetres, and a per-second rate to millimetres per frame: rounded once, here.
int32_t mm(double metres) {
    return static_cast<int32_t>(std::lround(metres * 1000.0));
}
int per_mille(double fraction) {
    return static_cast<int>(std::lround(std::clamp(fraction, 0.0, 1.0) * 1000.0));
}

int get_int(const baker::Array& a, const char* tag, int fallback) {
    return static_cast<int>(a.find_int(baker::Symbol(tag), fallback).value_or(fallback));
}
double get_float(const baker::Array& a, const char* tag, double fallback) {
    return a.find_float(baker::Symbol(tag), fallback).value_or(fallback);
}
std::string get_sym(const baker::Array& a, const char* tag, const char* fallback) {
    auto s = a.find_sym(baker::Symbol(tag), baker::Symbol(fallback));
    return s ? std::string(s->str()) : std::string(fallback);
}

// The keyed children of (tag ...) at the root: (fighters (kestrel ...) (wren ...)).
const baker::Array* section(const baker::Array& root, const char* tag) {
    return root.try_find(baker::Symbol(tag));
}

bool read_brains(const baker::Array* list, std::vector<Brain>& out) {
    if (!list) return true;
    for (const baker::Array& b : list->subarrays()) {
        Brain brain;
        brain.name = base::name(b);
        brain.reaction = std::clamp(get_int(b, "reaction", brain.reaction), 0, 63);
        brain.block = per_mille(get_float(b, "block", 0));
        brain.aggression = per_mille(get_float(b, "aggression", 0));
        brain.combo = per_mille(get_float(b, "combo", 0));
        brain.punish = per_mille(get_float(b, "punish", 0));
        brain.mash = per_mille(get_float(b, "mash", 0));
        brain.approach = per_mille(get_float(b, "approach", 0.5));
        brain.decide_every = std::max(1, get_int(b, "decide_every", brain.decide_every));
        out.push_back(std::move(brain));
    }
    return true;
}

Input button_named(const std::string& s) {
    if (s == "heavy") return kHeavy;
    if (s == "kick") return kKick;
    if (s == "special") return kSpecial;
    return kLight;
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

bool can_act(const Fighter& f) {
    return f.state == State::Idle || f.state == State::Walk || f.state == State::Crouch;
}

}  // namespace

int Roster::fighter(const std::string& name) const {
    for (size_t i = 0; i < fighters.size(); ++i)
        if (fighters[i].name == name) return static_cast<int>(i);
    return -1;
}

const Brain* Roster::find_brain(const std::string& name) const {
    for (const Brain& b : cpu)
        if (b.name == name) return &b;
    for (const Brain& b : bots)
        if (b.name == name) return &b;
    return nullptr;
}

bool load(const baker::Array& root, Roster& out, std::string& error) {
    Roster r;
    const baker::Array* fighters = section(root, "fighters");
    if (!fighters) {
        error = "no (fighters ...) in the data";
        return false;
    }
    for (const baker::Array& fa : fighters->subarrays()) {
        FighterData d;
        d.name = base::name(fa);
        d.health = get_int(fa, "health", d.health);
        d.width = mm(get_float(fa, "width", 0.6));
        d.height = mm(get_float(fa, "height", 1.8));
        d.crouch_height = mm(get_float(fa, "crouch_height", 1.1));
        d.walk_forward = mm(get_float(fa, "walk_forward", 2.4) / 60.0);
        d.walk_back = mm(get_float(fa, "walk_back", 1.8) / 60.0);
        const baker::Array* moves = fa.try_find(baker::Symbol("moves"));
        std::vector<std::vector<std::string>> cancels;
        if (moves)
            for (const baker::Array& ma : moves->subarrays()) {
                Move m;
                m.name = base::name(ma);
                m.button = button_named(get_sym(ma, "button", "light"));
                m.crouching = get_int(ma, "crouching", 0) != 0;
                m.startup = std::max(1, get_int(ma, "startup", 1));
                m.active = std::max(1, get_int(ma, "active", 1));
                m.recovery = std::max(1, get_int(ma, "recovery", 1));
                m.damage = get_int(ma, "damage", 0);
                m.hitstun = get_int(ma, "hitstun", 0);
                m.blockstun = get_int(ma, "blockstun", 0);
                m.push = mm(get_float(ma, "push", 0.15));
                const std::string guard = get_sym(ma, "guard", "mid");
                m.guard = guard == "low"        ? Guard::Low
                          : guard == "overhead" ? Guard::Overhead
                                                : Guard::Mid;
                if (const baker::Array* hb = ma.try_find(baker::Symbol("hitbox"));
                    hb && hb->size() >= 5) {
                    double v[4];
                    for (int i = 0; i < 4; ++i) v[i] = (*hb)[size_t(i) + 1].as_float().value_or(0);
                    m.hitbox = {mm(v[0]), mm(v[1]), mm(v[2]), mm(v[3])};
                }
                std::vector<std::string> names;
                if (const baker::Array* c = ma.try_find(baker::Symbol("cancels")))
                    for (size_t i = 1; i < c->size(); ++i)
                        if (auto s = (*c)[i].as_sym()) names.emplace_back(s->str());
                cancels.push_back(std::move(names));
                d.moves.push_back(std::move(m));
            }
        if (d.moves.empty() || d.moves.size() > 32) {
            error = "fighter " + d.name + " needs 1 to 32 moves";
            return false;
        }
        for (size_t i = 0; i < d.moves.size(); ++i)
            for (const std::string& name : cancels[i]) {
                auto it = std::find_if(d.moves.begin(), d.moves.end(),
                                       [&](const Move& m) { return m.name == name; });
                if (it == d.moves.end()) {
                    error = d.name + "." + d.moves[i].name + " cancels into " + name +
                            ", which is not one of its moves";
                    return false;
                }
                d.moves[i].cancels |= 1u << (it - d.moves.begin());
            }
        r.fighters.push_back(std::move(d));
    }
    if (r.fighters.empty()) {
        error = "no fighters in (fighters ...)";
        return false;
    }
    read_brains(section(root, "cpu"), r.cpu);
    read_brains(section(root, "bots"), r.bots);
    out = std::move(r);
    return true;
}

Rect hurtbox(const Fighter& f, const FighterData& d) {
    return {f.x - d.width / 2, 0, d.width, f.crouching ? d.crouch_height : d.height};
}

bool hitbox(const Fighter& f, const FighterData& d, Rect& out) {
    if (f.state != State::Attack || f.move < 0 || f.move >= int(d.moves.size())) return false;
    const Move& m = d.moves[size_t(f.move)];
    if (f.move_frame <= m.startup || f.move_frame > m.startup + m.active) return false;
    out = m.hitbox;
    out.x = f.facing > 0 ? f.x + m.hitbox.x : f.x - m.hitbox.x - m.hitbox.w;
    return true;
}

void start_round(World& w, const Roster& roster, const Rules& rules) {
    for (int i = 0; i < 2; ++i) {
        Fighter& f = w.f[i];
        f = Fighter{};
        f.x = (i == 0 ? -1 : 1) * (rules.start_gap / 2);
        f.facing = static_cast<int8_t>(i == 0 ? 1 : -1);
        f.health = roster.fighters[size_t(w.fighter_data[i])].health;
    }
    w.round_frame = 0;
    w.pause = 0;
    w.ko = 0;
}

namespace {

// A new press of an attack button, with down held or not, picks a move; it waits in the
// buffer until its fighter can act on it.
void press(Fighter& f, const FighterData& d, const Rules& rules) {
    const Input pressed = static_cast<Input>(f.held & ~f.previous & kAttackButtons);
    if (!pressed) return;
    const bool down = (f.held & kDown) != 0;
    for (Input b : {kSpecial, kHeavy, kKick, kLight}) {
        if (!(pressed & b)) continue;
        int found = -1;
        for (size_t i = 0; i < d.moves.size(); ++i)
            if (d.moves[i].button == b && d.moves[i].crouching == down) {
                found = int(i);
                break;
            }
        for (size_t i = 0; found < 0 && i < d.moves.size(); ++i)
            if (d.moves[i].button == b) found = int(i);
        if (found >= 0) {
            f.buffered = found;
            f.buffer_left = std::max(1, rules.buffer);
            return;
        }
    }
}

void advance(Fighter& f, const FighterData& d) {
    switch (f.state) {
        case State::Attack:
            if (f.move < 0 || f.move >= int(d.moves.size()) ||
                ++f.move_frame > d.moves[size_t(f.move)].total()) {
                f.state = State::Idle;
                f.move = -1;
                f.crouching = false;
            }
            break;
        case State::Hitstun:
        case State::Blockstun:
            if (--f.timer <= 0) {
                if (f.state == State::Hitstun) f.combo_hits = f.combo_damage = 0;
                f.state = State::Idle;
                f.blocking = false;
            }
            break;
        default: break;
    }
}

void start_move(Fighter& f, const FighterData& d, int m, int frame) {
    f.state = State::Attack;
    f.move = m;
    f.move_frame = 1;
    f.connected = false;
    f.blocking = false;
    f.crouching = d.moves[size_t(m)].crouching;
    f.buffered = -1;
    f.attack_frame = frame;
}

void act(Fighter& f, const FighterData& d, const Fighter& other, int frame) {
    const int toward = other.x >= f.x ? 1 : -1;
    const bool back = (f.held & (toward > 0 ? kLeft : kRight)) != 0;
    const bool forward = (f.held & (toward > 0 ? kRight : kLeft)) != 0;
    if (f.buffered >= 0) {
        const bool cancel = f.state == State::Attack && f.connected && f.move >= 0 &&
                            (d.moves[size_t(f.move)].cancels >> f.buffered & 1u);
        if (can_act(f) || cancel)
            start_move(f, d, f.buffered, frame);
        else if (--f.buffer_left <= 0)
            f.buffered = -1;
    }
    if (f.state == State::Blockstun) f.crouching = (f.held & kDown) != 0;
    if (!can_act(f)) return;
    f.crouching = (f.held & kDown) != 0;
    f.blocking = back;
    if (f.crouching) {
        f.state = State::Crouch;
    } else if (forward) {
        f.state = State::Walk;
        f.x += toward * d.walk_forward;
    } else if (back) {
        f.state = State::Walk;
        f.x -= toward * d.walk_back;
    } else {
        f.state = State::Idle;
    }
}

// Bodies do not overlap, and stay between the walls.
void separate(World& w, const Roster& roster, const Rules& rules) {
    const FighterData& d0 = roster.fighters[size_t(w.fighter_data[0])];
    const FighterData& d1 = roster.fighters[size_t(w.fighter_data[1])];
    const int l = w.f[0].x <= w.f[1].x ? 0 : 1, r = 1 - l;
    const int32_t wl = (l == 0 ? d0 : d1).width, wr = (r == 0 ? d0 : d1).width;
    const int32_t gap = (wl + wr) / 2;
    int32_t& xl = w.f[l].x;
    int32_t& xr = w.f[r].x;
    if (xr - xl < gap) {
        const int32_t mid = xl + (xr - xl) / 2;
        xl = mid - gap / 2;
        xr = xl + gap;
    }
    const int32_t lo = -rules.half_width + wl / 2, hi = rules.half_width - wr / 2;
    if (xl < lo) {
        xl = lo;
        xr = std::max(xr, xl + gap);
    }
    if (xr > hi) {
        xr = hi;
        xl = std::min(xl, xr - gap);
    }
    xl = std::max(xl, lo);
}

int32_t clamp_to_stage(int32_t x, int32_t width, const Rules& rules) {
    return std::clamp(x, -rules.half_width + width / 2, rules.half_width - width / 2);
}

}  // namespace

void step(World& w, const Roster& roster, const Rules& rules, const Input input[2]) {
    w.last = {};
    ++w.frame;
    const FighterData* d[2] = {&roster.fighters[size_t(w.fighter_data[0])],
                               &roster.fighters[size_t(w.fighter_data[1])]};
    for (int i = 0; i < 2; ++i) {
        w.f[i].previous = w.f[i].held;
        w.f[i].held = input[i];
        press(w.f[i], *d[i], rules);  // presses buffer through hit pause and stun
    }
    if (w.ko > 0) {
        if (--w.ko == 0) start_round(w, roster, rules);
        return;
    }
    if (w.pause > 0) {
        --w.pause;
        return;
    }
    ++w.round_frame;
    for (int i = 0; i < 2; ++i) advance(w.f[i], *d[i]);
    for (int i = 0; i < 2; ++i) act(w.f[i], *d[i], w.f[1 - i], w.frame);
    separate(w, roster, rules);
    for (int i = 0; i < 2; ++i)
        w.f[i].facing = static_cast<int8_t>(w.f[1 - i].x >= w.f[i].x ? 1 : -1);

    // Contacts are found for both before either is applied: a trade hits both.
    bool contact[2] = {false, false};
    const int moving[2] = {w.f[0].move, w.f[1].move};  // a hit clears the defender's
    for (int a = 0; a < 2; ++a) {
        Rect hb;
        contact[a] = !w.f[a].connected && hitbox(w.f[a], *d[a], hb) &&
                     overlaps(hb, hurtbox(w.f[1 - a], *d[1 - a]));
    }
    for (int a = 0; a < 2; ++a) {
        if (!contact[a]) continue;
        Fighter& at = w.f[a];
        Fighter& df = w.f[1 - a];
        const Move& m = d[a]->moves[size_t(moving[a])];
        const bool guard_ok = m.guard == Guard::Mid || (m.guard == Guard::Low && df.crouching) ||
                              (m.guard == Guard::Overhead && !df.crouching);
        const bool blocked = df.blocking && guard_ok && !contact[1 - a] &&
                             (can_act(df) || df.state == State::Blockstun);
        at.connected = true;
        if (blocked) {
            df.state = State::Blockstun;
            df.timer = m.blockstun + 1;
            w.last = {Event::Block, a, m.on_block()};
        } else {
            const int stun = std::max(1, m.hitstun - rules.stun_decay * df.combo_hits);
            df.health -= m.damage;
            ++df.combo_hits;
            df.combo_damage += m.damage;
            df.state = State::Hitstun;
            df.timer = stun + 1;
            df.move = -1;
            df.blocking = false;
            w.last = {Event::Hit, a, stun + 1 - m.active - m.recovery};
        }
        // Push the defender away; what a wall stops pushes the attacker back instead.
        const int dir = df.x >= at.x ? 1 : -1;
        const int32_t wanted = df.x + dir * m.push;
        df.x = clamp_to_stage(wanted, d[1 - a]->width, rules);
        at.x = clamp_to_stage(at.x - (wanted - df.x), d[a]->width, rules);
        w.pause = rules.hit_pause;
    }

    // The round ends at a knockout or at its time limit.
    const bool down[2] = {w.f[0].health <= 0, w.f[1].health <= 0};
    int winner = -2;  // -2: still fighting; -1: a draw
    if (down[0] || down[1]) {
        winner = down[0] && down[1] ? -1 : down[0] ? 1 : 0;
        w.last.kind = Event::Ko;
    } else if (w.round_frame >= rules.round_frames) {
        winner = w.f[0].health == w.f[1].health ? -1 : w.f[0].health > w.f[1].health ? 0 : 1;
        w.last.kind = Event::TimeUp;
    }
    if (winner == -2) return;
    for (int i = 0; i < 2; ++i)
        if (down[i]) {
            w.f[i].health = 0;
            w.f[i].state = State::Down;
        }
    if (winner >= 0)
        ++w.rounds[winner];
    else
        ++w.draws;
    w.ko = std::max(1, rules.ko_frames);
}

// --- Pilot: the CPU and the bots. ---

void Pilot::reset(const Brain* brain, uint64_t seed) {
    brain_ = brain;
    rng_ = base::Rng(seed, 0x632BE59BD9B4E019ull);
    seen_count_ = 0;
    plan_ = 0;
    next_decision_ = 0;
    answered_attack_ = -1;
    punished_ = -1;
    guarding_ = false;
    guard_low_ = false;
    blocked_move_ = -1;
    fastest_ = -1;
}

uint32_t Pilot::roll() noexcept {
    return static_cast<uint32_t>((rng_.next() >> 33) % 1000u);
}

Input Pilot::think(const World& w, const Roster& roster, int me) {
    if (!brain_) return 0;
    const Brain& b = *brain_;
    const Fighter& self = w.f[me];
    const FighterData& mine = roster.fighters[size_t(w.fighter_data[me])];
    const FighterData& theirs = roster.fighters[size_t(w.fighter_data[1 - me])];

    // What it sees of the opponent is `reaction` frames old.
    seen_[seen_count_ % kHistory] = w.f[1 - me];
    ++seen_count_;
    const int delay = std::min({b.reaction, seen_count_ - 1, kHistory - 1});
    const Fighter& seen = seen_[(seen_count_ - 1 - delay) % kHistory];

    if (w.ko > 0 || self.state == State::Down) {
        plan_ = 0;
        guarding_ = false;
        return 0;
    }
    const Input toward = w.f[1 - me].x >= self.x ? kRight : kLeft;
    const Input away = toward == kRight ? kLeft : kRight;
    const int32_t dist = std::abs(seen.x - self.x);
    auto press_move = [&](int m) {
        const Move& mv = mine.moves[size_t(m)];
        return static_cast<Input>(mv.button | (mv.crouching ? kDown : 0));
    };
    auto pick_in_range = [&](Input& out) {
        int in_range[32];
        int n = 0;
        for (size_t i = 0; i < mine.moves.size(); ++i) {
            const Move& m = mine.moves[i];
            if (m.hitbox.x + m.hitbox.w + theirs.width / 2 >= dist) in_range[n++] = int(i);
        }
        if (n == 0) return false;
        out = press_move(in_range[roll() % uint32_t(n)]);
        return true;
    };

    // A move that connected: cancel into its most damaging follow-up.
    if (self.state == State::Attack && self.connected && self.buffered < 0 && self.move >= 0 &&
        self.move_frame == mine.moves[size_t(self.move)].startup + 2) {
        const uint32_t cancels = mine.moves[size_t(self.move)].cancels;
        int best = -1;
        for (size_t i = 0; i < mine.moves.size(); ++i)
            if ((cancels >> i & 1u) &&
                (best < 0 || mine.moves[i].damage > mine.moves[size_t(best)].damage))
                best = int(i);
        if (best >= 0 && chance(b.combo)) return press_move(best);
    }

    // The most damaging move in range that lands within `frames`; false if none does.
    auto punisher = [&](int frames, Input& out) {
        int best = -1;
        for (size_t i = 0; i < mine.moves.size(); ++i) {
            const Move& m = mine.moves[i];
            if (m.startup + 1 <= frames && m.hitbox.x + m.hitbox.w + theirs.width / 2 >= dist &&
                (best < 0 || m.damage > mine.moves[size_t(best)].damage))
                best = int(i);
        }
        if (best < 0) return false;
        out = press_move(best);
        return true;
    };

    // Out of blockstun: a blocked move it knows is unsafe, it punishes, as a player does who
    // knows the frame data; no reaction needed, it felt the block.
    if (self.state == State::Blockstun && w.f[1 - me].state == State::Attack)
        blocked_move_ = w.f[1 - me].move;
    if (blocked_move_ >= 0 && can_act(self)) {
        const int move = blocked_move_;
        blocked_move_ = -1;
        Input attack = 0;
        if (move < int(theirs.moves.size()) && chance(b.punish) &&
            punisher(-theirs.moves[size_t(move)].on_block(), attack)) {
            guarding_ = false;
            plan_ = 0;
            return attack;
        }
    }

    // A move it sees recovering, close enough to answer: punish it, once per move, with what
    // lands before the recovery it has not yet seen ends.
    const bool seen_attacking =
        seen.state == State::Attack && seen.move >= 0 && seen.move < int(theirs.moves.size());
    if (seen_attacking && can_act(self) && seen.attack_frame != punished_) {
        const Move& m = theirs.moves[size_t(seen.move)];
        if (seen.move_frame > m.startup + m.active) {
            punished_ = seen.attack_frame;
            Input attack = 0;
            if (chance(b.punish) && punisher(m.total() - seen.move_frame - delay, attack)) {
                guarding_ = false;
                plan_ = 0;
                return attack;
            }
        }
    }

    // An attack it sees: it may raise its guard if it was not guarding, and it guards low
    // for a low; either is a reaction, and timed. Whether it lands in time is the move's
    // startup against the pilot's reaction.
    if (seen_attacking && seen.attack_frame != answered_attack_ &&
        (can_act(self) || self.state == State::Blockstun)) {
        answered_attack_ = seen.attack_frame;
        const bool low = theirs.moves[size_t(seen.move)].guard == Guard::Low;
        const bool raise = !guarding_ && chance(b.block);
        if (raise || (guarding_ && low != guard_low_)) {
            guarding_ = true;
            guard_low_ = low;
            const int took = w.frame - seen.attack_frame;
            if (fastest_ < 0 || took < fastest_) fastest_ = took;
        }
    }

    Input pulse = 0;
    if (b.mash && chance(b.mash)) {
        const Input buttons[4] = {kLight, kHeavy, kKick, kSpecial};
        pulse = buttons[roll() % 4];
        if (roll() % 3 == 0) pulse = static_cast<Input>(pulse | kDown);
    }

    // Every few frames: guard, attack, or walk.
    if (w.frame >= next_decision_ && (can_act(self) || self.state == State::Blockstun)) {
        next_decision_ = w.frame + b.decide_every;
        int32_t reach = 0;
        for (const Move& m : theirs.moves) reach = std::max(reach, m.hitbox.x + m.hitbox.w);
        const bool threatened = dist <= reach + mine.width / 2 + 300;
        Input attack = 0;
        // A guard holds while the opponent presses: it is dropped only between attacks.
        const bool pressed = seen_attacking || self.state == State::Blockstun;
        if (guarding_ && pressed && threatened) {
            // keep guarding
        } else if (threatened && chance(b.block)) {
            if (!guarding_) guard_low_ = false;
            guarding_ = true;
        } else if (self.state != State::Blockstun && chance(b.aggression) &&
                   pick_in_range(attack)) {
            guarding_ = false;
            plan_ = 0;
            return static_cast<Input>(attack | pulse);
        } else {
            guarding_ = false;
            plan_ = chance(b.approach) ? toward : away;
        }
    }
    if (guarding_) return static_cast<Input>(away | (guard_low_ ? kDown : 0));
    return static_cast<Input>(plan_ | pulse);
}

// --- The analyses. ---

std::vector<FrameRow> frame_table(const Roster& roster) {
    std::vector<FrameRow> rows;
    for (const FighterData& d : roster.fighters)
        for (const Move& m : d.moves)
            rows.push_back({d.name, m.name, m.startup, m.active, m.recovery, m.damage, m.on_hit(),
                            m.on_block()});
    return rows;
}

namespace {

struct Search {
    const Roster& roster;
    const Rules& rules;
    const FighterData& d;
    int max_hits;
    ComboReport report;
    struct Best {
        int damage = 0;
        int hits = 0;
        bool infinite = false;
        std::string route;
    };
    std::unordered_map<uint64_t, Best> memo;

    static uint64_t key(const World& w) {
        const Fighter& a = w.f[0];
        const Fighter& b = w.f[1];
        uint64_t k = uint64_t(uint32_t(a.x + 0x8000)) & 0xFFFF;
        k = k << 16 | (uint64_t(uint32_t(b.x + 0x8000)) & 0xFFFF);
        k = k << 8 | uint64_t(uint8_t(a.move));
        k = k << 8 | uint64_t(uint8_t(a.move_frame));
        k = k << 8 | uint64_t(uint8_t(b.combo_hits));
        return k;
    }

    Input press_of(int m) const {
        const Move& mv = d.moves[size_t(m)];
        return static_cast<Input>(mv.button | (mv.crouching ? kDown : 0));
    }

    // Holds forward for `walk` frames, then presses move `m`; true if it hits as part of
    // the combo (the defender still in hitstun), with `w` at that contact.
    bool attempt(World& w, int walk, int m) const {
        const int hits = w.f[1].combo_hits;
        for (int t = 0; t < 200; ++t) {
            // Once the defender recovers, no later hit is part of this combo.
            if (w.f[1].state != State::Hitstun && w.pause == 0) return false;
            const Input press = static_cast<Input>(t < walk ? kRight : t == walk ? press_of(m) : 0);
            const Input in[2] = {press, 0};
            step(w, roster, rules, in);
            if (w.last.kind == Event::Hit && w.last.attacker == 0)
                return w.f[1].combo_hits == hits + 1;
        }
        return false;
    }

    // The best continuation from a contact, by damage.
    Best follow(const World& w) {
        ++report.searched;
        if (w.f[1].combo_hits >= max_hits) return {0, 0, true, ""};
        const uint64_t k = key(w);
        if (auto it = memo.find(k); it != memo.end()) return it->second;
        Best best;
        // The longest a defender stays stunned bounds how long to wait before pressing.
        const int window = w.pause + w.f[1].timer + 1;
        for (size_t m = 0; m < d.moves.size(); ++m)
            for (int walk = 0; walk <= window; ++walk) {
                World next = w;
                if (!attempt(next, walk, int(m))) continue;
                const Best rest = follow(next);
                const int damage = d.moves[m].damage + rest.damage;
                if (rest.infinite || damage > best.damage) {
                    best = {damage, rest.hits + 1, rest.infinite,
                            d.moves[m].name + (rest.route.empty() ? "" : " > " + rest.route)};
                    if (rest.infinite) {
                        memo[k] = best;
                        return best;
                    }
                }
            }
        memo[k] = best;
        return best;
    }
};

}  // namespace

ComboReport combo_search(const Roster& roster, const Rules& rules, int max_hits) {
    ComboReport report;
    for (size_t fi = 0; fi < roster.fighters.size(); ++fi) {
        const FighterData& d = roster.fighters[fi];
        Search s{roster, rules, d, max_hits, {}, {}};
        const int32_t touching = d.width;
        for (int corner = 0; corner < 2; ++corner) {
            World base;
            base.fighter_data[0] = base.fighter_data[1] = int(fi);
            start_round(base, roster, rules);
            const int32_t defender = corner ? rules.half_width - d.width / 2 : touching / 2;
            base.f[1].x = defender;
            base.f[0].x = defender - touching;
            base.f[1].health = base.f[0].health = 1 << 30;  // no knockouts in a search
            for (size_t m = 0; m < d.moves.size(); ++m) {
                World w = base;
                // The opener, from neutral: it must hit a defender standing still.
                const Input press = s.press_of(int(m));
                bool hit = false;
                for (int t = 0; t < 200 && !hit; ++t) {
                    const Input in[2] = {static_cast<Input>(t == 0 ? press : 0), 0};
                    step(w, roster, rules, in);
                    hit = w.last.kind == Event::Hit;
                    if (t > 0 && w.f[0].state != State::Attack) break;
                }
                if (!hit) continue;
                const Search::Best rest = s.follow(w);
                const int damage = d.moves[m].damage + rest.damage;
                const std::string route = d.name + ": " + d.moves[m].name +
                                          (rest.route.empty() ? "" : " > " + rest.route);
                if (rest.infinite && !report.infinite) {
                    report.infinite = true;
                    report.infinite_route = route;
                }
                if (damage > report.max_damage) {
                    report.max_damage = damage;
                    report.max_hits = rest.hits + 1;
                    report.route = route;
                }
            }
        }
        report.searched += s.report.searched;
    }
    return report;
}

}  // namespace duel
