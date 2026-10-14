// Duel (game-stacks.md §4.6): player versus computer fighting, on frame data. The fight is
// fight.hpp, in integers; this file is the game around it: input, the script words, the
// drawing, and the frame-data workbench (F1).
//
//   stacks_duel                       play: you are on the left, the CPU on the right
//   stacks_duel sim --seeds 1-10      the CPU against the bots, judged by metrics/
//   stacks_duel eval "{duel.frame_table}" "{duel.combo_route}"
//
// Keys: A and D (or the arrows) walk, S crouches, back blocks; J light, K heavy, L kick, I
// special. A gamepad: the stick or pad, and X, Y, A, B. P pauses; period steps one frame.
#include "fight.hpp"

#include <base/app.hpp>
#include <base/render.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <moira/std_interop.hpp>
#include <moira/std_script.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static moira::Tweak<int> kHitPause{
    "duel.hit_pause", 6, {.range = {0, 30}, .desc = "Frames both fighters freeze on contact"}};
static moira::Tweak<int> kBuffer{
    "duel.buffer", 5, {.range = {1, 20}, .desc = "Frames a press waits to be acted on"}};
static moira::Tweak<int> kStunDecay{
    "duel.stun_decay", 1, {.range = {0, 10}, .desc = "Hitstun each combo hit loses, in frames"}};
static moira::Tweak<float> kHalfWidth{
    "duel.stage_half_width", 4.0f, {.range = {2, 20}, .desc = "Metres from center to a wall"}};
static moira::Tweak<float> kStartGap{
    "duel.start_gap", 2.0f, {.range = {0.5f, 10}, .desc = "Metres between fighters at a start"}};
static moira::Tweak<int> kRoundSeconds{
    "duel.round_seconds", 60, {.range = {5, 600}, .desc = "A round's time limit"}};
static moira::Tweak<int> kKoFrames{
    "duel.ko_frames", 90, {.range = {1, 600}, .desc = "Frames from a knockout to the next round"}};
static moira::Tweak<bool> kShowBoxes{"duel.show_boxes", true, {.desc = "Draw active hitboxes"}};

namespace {

using duel::Input;

constexpr float kMetres = 0.001f;  // the fight counts millimetres

duel::Rules current_rules() {
    duel::Rules r;
    r.hit_pause = kHitPause;
    r.buffer = kBuffer;
    r.stun_decay = kStunDecay;
    r.half_width = static_cast<int32_t>(kHalfWidth * 1000.0f + 0.5f);
    r.start_gap = static_cast<int32_t>(kStartGap * 1000.0f + 0.5f);
    r.round_frames = kRoundSeconds * 60;
    r.ko_frames = kKoFrames;
    return r;
}

moira::Expected<int> side_index(int64_t side) {
    if (side != 1 && side != 2)
        return moira::Unexpected(
            moira::make_error(moira::ErrorCode::Range, "a side is 1 (left) or 2 (right)"));
    return static_cast<int>(side - 1);
}

class DuelGame final : public base::Game {
public:
    void bind(moira::Context& ctx) override {
        auto doc = [](auto f, const char* text) {
            if (f) f->doc(text);
        };
        doc(ctx.func("duel.reset", [this] { new_match(); }),
            "A new match: rounds to zero, both fighters at their marks, the pilots reseeded.");
        doc(ctx.func("duel.p1", [this](moira::Symbol who) { return control(0, who); }),
            "Who plays the left side: human, dummy, block (a dummy that holds back), a CPU "
            "level (cpu), or a bot (bots).");
        doc(ctx.func("duel.p2", [this](moira::Symbol who) { return control(1, who); }),
            "Who plays the right side; as duel.p1.");
        doc(ctx.func("duel.fighter",
                     [this](int64_t side, moira::Symbol name) -> moira::Expected<void> {
                         auto s = side_index(side);
                         if (!s) return moira::Unexpected(s.error());
                         const int f = roster_.fighter(std::string(name.str()));
                         if (f < 0)
                             return moira::Unexpected(moira::make_error(
                                 moira::ErrorCode::NotFound,
                                 "no fighter " + std::string(name.str()) + " in (fighters ...)"));
                         world_.fighter_data[*s] = f;
                         new_match();
                         return {};
                     }),
            "Puts a fighter of (fighters ...) on a side, and starts a new match.");
        doc(ctx.func("duel.rounds",
                     [this](int64_t side) -> moira::Expected<int64_t> {
                         auto s = side_index(side);
                         if (!s) return moira::Unexpected(s.error());
                         return world_.rounds[*s];
                     }),
            "Rounds a side has won since the match started.");
        doc(ctx.func(
                "duel.round_count",
                [this] { return int64_t{world_.rounds[0] + world_.rounds[1] + world_.draws}; }),
            "Rounds finished since the match started, draws included.");
        doc(ctx.func("duel.win_rate",
                     [this](int64_t side) -> moira::Expected<double> {
                         auto s = side_index(side);
                         if (!s) return moira::Unexpected(s.error());
                         const int n = world_.rounds[0] + world_.rounds[1] + world_.draws;
                         return n ? double(world_.rounds[*s]) / n : 0.0;
                     }),
            "The share of finished rounds a side won; 0 before any.");
        doc(ctx.func("duel.health",
                     [this](int64_t side) -> moira::Expected<int64_t> {
                         auto s = side_index(side);
                         if (!s) return moira::Unexpected(s.error());
                         return world_.f[*s].health;
                     }),
            "A side's health now.");
        doc(ctx.func("duel.fastest_reaction",
                     [this](int64_t side) -> moira::Expected<int64_t> {
                         auto s = side_index(side);
                         if (!s) return moira::Unexpected(s.error());
                         return pilots_[*s].fastest_reaction();
                     }),
            "The fewest frames a side's pilot took to block an attack, from the attack's "
            "first frame; -1 if it never blocked.");
        doc(ctx.func("duel.on_hit",
                     [this](moira::Symbol fighter, moira::Symbol move) -> moira::Expected<int64_t> {
                         auto m = find_move(fighter, move);
                         if (!m) return moira::Unexpected(m.error());
                         return (*m)->on_hit();
                     }),
            "Frames the attacker is ahead when the move hits on its first active frame.");
        doc(ctx.func("duel.on_block",
                     [this](moira::Symbol fighter, moira::Symbol move) -> moira::Expected<int64_t> {
                         auto m = find_move(fighter, move);
                         if (!m) return moira::Unexpected(m.error());
                         return (*m)->on_block();
                     }),
            "Frames the attacker is ahead when the move is blocked; negative is unsafe.");
        doc(ctx.func("duel.max_block_advantage",
                     [this] {
                         int64_t best = -1000;
                         for (const duel::FrameRow& r : duel::frame_table(roster_))
                             best = std::max<int64_t>(best, r.on_block);
                         return best;
                     }),
            "The most any move of any fighter is ahead on block.");
        doc(ctx.func("duel.frame_table", [this] { return frame_table_text(); }),
            "Every move's frame data and advantage, as a table.");
        doc(ctx.func("duel.max_combo_damage", [this] { return int64_t{combos().max_damage}; }),
            "The most damage a combo found by the search does.");
        doc(ctx.func("duel.infinites", [this] { return int64_t{combos().infinite ? 1 : 0}; }),
            "1 if the combo search found a combo that does not end (it reached its hit cap).");
        doc(ctx.func("duel.combo_route",
                     [this] {
                         const duel::ComboReport& c = combos();
                         return c.infinite ? "infinite: " + c.infinite_route : c.route;
                     }),
            "The most damaging combo the search found, or an infinite one.");
    }

    void load(const moira::Config& cfg) override {
        duel::Roster next;
        std::string error;
        if (!duel::load(*cfg.root(), next, error)) {
            std::fprintf(stderr, "duel: %s; keeping the last good roster\n", error.c_str());
            return;
        }
        roster_ = std::move(next);
        loaded_ = true;
        combos_valid_ = false;
        // The world keeps going on the new data; what no longer exists is dropped.
        for (int i = 0; i < 2; ++i) {
            if (world_.fighter_data[i] >= int(roster_.fighters.size())) world_.fighter_data[i] = 0;
            duel::Fighter& f = world_.f[i];
            const auto& moves = roster_.fighters[size_t(world_.fighter_data[i])].moves;
            if (f.move >= int(moves.size())) {
                f.state = duel::State::Idle;
                f.move = -1;
            }
            if (f.buffered >= int(moves.size())) f.buffered = -1;
            pilots_[i].reset(pilot_name_[i].empty() ? nullptr : roster_.find_brain(pilot_name_[i]),
                             seed_ * 2 + uint64_t(i));
        }
    }

    void reset(uint64_t seed) override {
        seed_ = seed;
        mode_[0] = Mode::Human;
        mode_[1] = Mode::Pilot;
        pilot_name_[0].clear();
        pilot_name_[1] = "normal";
        new_match();
    }

    bool start(base::App&) override {
        if (!loaded_) std::fprintf(stderr, "duel: the data has no usable roster\n");
        return loaded_;
    }

    void step(double) override {
        if (!loaded_ || (paused_ && !advance_one_)) return;
        advance_one_ = false;
        previous_ = world_;
        Input in[2];
        for (int i = 0; i < 2; ++i) in[i] = input(i);
        duel::step(world_, roster_, current_rules(), in);
        if (world_.last.kind == duel::Event::Hit || world_.last.kind == duel::Event::Block)
            contact_ = world_.last;
    }

    void event(const SDL_Event& e) override {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad_) pad_ = SDL_OpenGamepad(e.gdevice.which);
        if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) return;
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        if (down && !e.key.repeat) {
            if (e.key.key == SDLK_P) paused_ = !paused_;
            if (e.key.key == SDLK_PERIOD) advance_one_ = true;
        }
        Input bit = 0;
        switch (e.key.key) {
            case SDLK_A:
            case SDLK_LEFT: bit = duel::kLeft; break;
            case SDLK_D:
            case SDLK_RIGHT: bit = duel::kRight; break;
            case SDLK_S:
            case SDLK_DOWN: bit = duel::kDown; break;
            case SDLK_J: bit = duel::kLight; break;
            case SDLK_K: bit = duel::kHeavy; break;
            case SDLK_L: bit = duel::kKick; break;
            case SDLK_I: bit = duel::kSpecial; break;
            default: return;
        }
        keys_ = static_cast<Input>(down ? keys_ | bit : keys_ & ~bit);
    }

    void draw(base::Renderer& r, float alpha) override {
        r.clear_color(0.08f, 0.08f, 0.10f);
        base::Camera cam;
        cam.eye[0] = 0;
        cam.eye[1] = 1.0f;
        cam.eye[2] = -6.5f;
        cam.at[1] = 0.6f;
        cam.fov_y_degrees = 50;
        r.camera(cam);
        if (!loaded_) return;

        const float half = kHalfWidth;
        box(r, 0, -0.05f, 0, 2 * half + 1, 0.1f, 3, 0.22f, 0.22f, 0.25f);
        box(r, -half - 0.05f, 1.5f, 0, 0.1f, 3, 3, 0.3f, 0.3f, 0.34f);
        box(r, half + 0.05f, 1.5f, 0, 0.1f, 3, 3, 0.3f, 0.3f, 0.34f);

        static constexpr float kColors[2][3] = {{0.25f, 0.55f, 0.95f}, {0.95f, 0.35f, 0.30f}};
        for (int i = 0; i < 2; ++i) {
            const duel::Fighter& f = world_.f[i];
            const duel::FighterData& d = roster_.fighters[size_t(world_.fighter_data[i])];
            const float x =
                (float(previous_.f[i].x) + float(f.x - previous_.f[i].x) * alpha) * kMetres;
            float c[3] = {kColors[i][0], kColors[i][1], kColors[i][2]};
            if (f.state == duel::State::Hitstun)
                for (float& v : c) v = std::min(1.0f, v + 0.45f);
            if (f.state == duel::State::Blockstun)
                for (float& v : c) v *= 0.55f;
            const float w = float(d.width) * kMetres;
            if (f.state == duel::State::Down) {
                box(r, x, w / 2, 0, float(d.height) * kMetres, w, 0.5f, c[0], c[1], c[2]);
                continue;
            }
            const float h = float(f.crouching ? d.crouch_height : d.height) * kMetres;
            box(r, x, h / 2, 0, w, h, 0.5f, c[0], c[1], c[2]);
            // A head, so the facing reads: toward the opponent, at the top.
            box(r, x + float(f.facing) * w * 0.3f, h - 0.12f, -0.1f, 0.12f, 0.12f, 0.3f, 1, 1, 1);
            duel::Rect hb;
            if (kShowBoxes && duel::hitbox(f, d, hb))
                box(r, (float(hb.x) + float(hb.w) / 2) * kMetres,
                    (float(hb.y) + float(hb.h) / 2) * kMetres, 0, float(hb.w) * kMetres,
                    float(hb.h) * kMetres, 0.62f, 1, 0.1f, 0.1f);
            // Health, along the top: the left side's bar grows from the middle outwards.
            const float full = half - 0.5f;
            const float share = float(std::max(0, f.health)) / float(std::max(1, d.health));
            const float sign = i == 0 ? -1.0f : 1.0f;
            box(r, sign * (0.3f + full / 2), 2.55f, 0, full, 0.18f, 0.1f, 0.15f, 0.15f, 0.15f);
            box(r, sign * (0.3f + full * share / 2), 2.55f, -0.06f, full * share, 0.16f, 0.1f, c[0],
                c[1], c[2]);
        }

        const int seconds_left =
            std::max(0, (current_rules().round_frames - world_.round_frame) / 60);
        r.text(48, 4, "P1 %d  -  %d P2    time %d%s", world_.rounds[0], world_.rounds[1],
               seconds_left, paused_ ? "    PAUSED (P; period steps)" : "");
        if (contact_.kind != duel::Event::None)
            r.text(48, 5, "last contact: P%d %s, %+d frames", contact_.attacker + 1,
                   contact_.kind == duel::Event::Hit ? "hit" : "blocked", contact_.advantage);
        for (int i = 0; i < 2; ++i)
            if (world_.f[i].combo_hits > 1)
                r.text(i == 0 ? 48 : 90, 6, "%d hits, %d damage", world_.f[i].combo_hits,
                       world_.f[i].combo_damage);
        r.text(48, 7, "P1 %s   P2 %s", who(0).c_str(), who(1).c_str());
    }

    void tools() override {
        ImGui::SetNextWindowPos(ImVec2(380, 470), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Duel")) {
            ImGui::End();
            return;
        }
        if (ImGui::Button(paused_ ? "Resume (P)" : "Pause (P)")) paused_ = !paused_;
        ImGui::SameLine();
        if (ImGui::Button("Step (.)")) advance_one_ = true;
        ImGui::SameLine();
        if (ImGui::Button("New match")) new_match();
        for (int i = 0; i < 2; ++i) {
            ImGui::PushID(i);
            const std::string current = who(i);
            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo(i == 0 ? "P1 (left)" : "P2 (right)", current.c_str())) {
                std::vector<std::string> names = {"human", "dummy", "block"};
                for (const auto* list : {&roster_.cpu, &roster_.bots})
                    for (const duel::Brain& b : *list) names.push_back(b.name);
                for (const std::string& name : names)
                    if (ImGui::Selectable(name.c_str(), name == current))
                        (void)control(i, moira::Symbol(name));
                ImGui::EndCombo();
            }
            if (i == 0) ImGui::SameLine();
            ImGui::PopID();
        }
        if (ImGui::BeginTable("frames", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            for (const char* h : {"fighter", "move", "startup", "active", "recovery", "damage",
                                  "on hit", "on block"})
                ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const duel::FrameRow& row : duel::frame_table(roster_)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.fighter.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.move.c_str());
                for (int v : {row.startup, row.active, row.recovery, row.damage}) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", v);
                }
                for (int v : {row.on_hit, row.on_block}) {
                    ImGui::TableNextColumn();
                    const ImVec4 color = v > 0    ? ImVec4(0.4f, 0.9f, 0.4f, 1)
                                         : v < -3 ? ImVec4(1, 0.45f, 0.4f, 1)
                                                  : ImVec4(0.9f, 0.9f, 0.9f, 1);
                    ImGui::TextColored(color, "%+d", v);
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Search combos")) combos_valid_ = false, (void)combos();
        if (combos_valid_) {
            ImGui::SameLine();
            ImGui::TextWrapped("%s: %d damage, %d hits (%d states)",
                               combos_.infinite ? "INFINITE" : "best", combos_.max_damage,
                               combos_.max_hits, combos_.searched);
            ImGui::TextWrapped("%s",
                               (combos_.infinite ? combos_.infinite_route : combos_.route).c_str());
        }
        ImGui::End();
    }

private:
    enum class Mode : uint8_t { Human, Dummy, Block, Pilot };

    static void box(base::Renderer& r, float x, float y, float z, float w, float h, float d,
                    float cr, float cg, float cb) {
        base::Box b;
        b.center[0] = x;
        b.center[1] = y;
        b.center[2] = z;
        b.size[0] = w;
        b.size[1] = h;
        b.size[2] = d;
        b.color[0] = cr;
        b.color[1] = cg;
        b.color[2] = cb;
        r.box(b);
    }

    void new_match() {
        if (!loaded_) return;
        const int keep[2] = {world_.fighter_data[0], world_.fighter_data[1]};
        world_ = duel::World{};
        world_.fighter_data[0] = keep[0];
        world_.fighter_data[1] = keep[1];
        duel::start_round(world_, roster_, current_rules());
        previous_ = world_;
        contact_ = {};
        for (int i = 0; i < 2; ++i)
            pilots_[i].reset(mode_[i] == Mode::Pilot ? roster_.find_brain(pilot_name_[i]) : nullptr,
                             seed_ * 2 + uint64_t(i));
    }

    moira::Expected<void> control(int side, moira::Symbol who) {
        const std::string name(who.str());
        Mode mode = Mode::Pilot;
        if (name == "human")
            mode = Mode::Human;
        else if (name == "dummy")
            mode = Mode::Dummy;
        else if (name == "block")
            mode = Mode::Block;
        else if (!roster_.find_brain(name))
            return moira::Unexpected(moira::make_error(
                moira::ErrorCode::NotFound,
                "no pilot " + name +
                    ": human, dummy, block, or a name in (cpu ...) or (bots ...)"));
        mode_[side] = mode;
        pilot_name_[side] = mode == Mode::Pilot ? name : std::string();
        pilots_[side].reset(mode == Mode::Pilot ? roster_.find_brain(name) : nullptr,
                            seed_ * 2 + uint64_t(side));
        return {};
    }

    std::string who(int side) const {
        switch (mode_[side]) {
            case Mode::Human: return "human";
            case Mode::Dummy: return "dummy";
            case Mode::Block: return "block";
            case Mode::Pilot: return pilot_name_[side];
        }
        return {};
    }

    Input input(int side) {
        switch (mode_[side]) {
            case Mode::Human: return static_cast<Input>(keys_ | pad_input());
            case Mode::Dummy: return 0;
            case Mode::Block:
                return world_.f[1 - side].x >= world_.f[side].x ? duel::kLeft : duel::kRight;
            case Mode::Pilot: return pilots_[side].think(world_, roster_, side);
        }
        return 0;
    }

    Input pad_input() const {
        if (!pad_) return 0;
        unsigned in = 0;  // widened while built: MSVC warns on |= into 8 bits
        const int x = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTY);
        auto held = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad_, b); };
        if (x < -16000 || held(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) in |= duel::kLeft;
        if (x > 16000 || held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) in |= duel::kRight;
        if (y > 16000 || held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) in |= duel::kDown;
        if (held(SDL_GAMEPAD_BUTTON_WEST)) in |= duel::kLight;
        if (held(SDL_GAMEPAD_BUTTON_NORTH)) in |= duel::kHeavy;
        if (held(SDL_GAMEPAD_BUTTON_SOUTH)) in |= duel::kKick;
        if (held(SDL_GAMEPAD_BUTTON_EAST)) in |= duel::kSpecial;
        return static_cast<Input>(in);
    }

    moira::Expected<const duel::Move*> find_move(moira::Symbol fighter, moira::Symbol move) const {
        const int f = roster_.fighter(std::string(fighter.str()));
        if (f >= 0)
            for (const duel::Move& m : roster_.fighters[size_t(f)].moves)
                if (m.name == move.str()) return &m;
        return moira::Unexpected(moira::make_error(
            moira::ErrorCode::NotFound,
            "no move " + std::string(fighter.str()) + "." + std::string(move.str())));
    }

    std::string frame_table_text() const {
        std::string s =
            "fighter    move          startup active recovery damage  on-hit on-block\n";
        char line[160];
        for (const duel::FrameRow& r : duel::frame_table(roster_)) {
            std::snprintf(line, sizeof line, "%-10s %-13s %7d %6d %8d %6d %+7d %+8d\n",
                          r.fighter.c_str(), r.move.c_str(), r.startup, r.active, r.recovery,
                          r.damage, r.on_hit, r.on_block);
            s += line;
        }
        return s;
    }

    const duel::ComboReport& combos() {
        if (!combos_valid_) {
            combos_ = duel::combo_search(roster_, current_rules());
            combos_valid_ = true;
        }
        return combos_;
    }

    duel::Roster roster_;
    bool loaded_ = false;
    duel::World world_, previous_;
    duel::Pilot pilots_[2];
    Mode mode_[2] = {Mode::Human, Mode::Pilot};
    std::string pilot_name_[2] = {"", "normal"};
    uint64_t seed_ = 0;
    duel::Event contact_{};
    duel::ComboReport combos_;
    bool combos_valid_ = false;
    bool paused_ = false, advance_one_ = false;
    Input keys_ = 0;
    SDL_Gamepad* pad_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    DuelGame game;
    return base::main(game, argc, argv, {.title = "Duel", .data_dir = DUEL_DATA});
}
