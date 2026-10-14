// Wayfarer (game-stacks.md §4.4): does exploring lead somewhere? The world and the analyses
// are world.hpp; this file is the game around them: a third-person character on Jolt's
// CharacterVirtual that walks, climbs, and glides; the world streamed by cell; the mission
// director, which evaluates each objective's goal command; the workbench (a map with the
// mission graph, a gating view); and the script words (mission., world.).
//
//   stacks_wayfarer                                    play
//   stacks_wayfarer sim --seeds 1-5                    softlocks, reachability, pacing per bot
//   stacks_wayfarer eval "{world.prove}" "{world.proof_report}"
//   stacks_wayfarer eval "{world.pacing curious 1800}" "{world.pacing_report}"
//
// Keys: WASD move (relative to the camera); Q and E, or the mouse with the right button
// held, turn the camera; space jumps, and held in the air glides (with the glider).
#include "world.hpp"

#include <base/app.hpp>
#include <base/physics.hpp>
#include <base/render.hpp>
#include <base/words.hpp>

#include <Jolt/Math/Trigonometry.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static baker::Tweak<float> kRunSpeed{
    "wayfarer.run_speed", 6.0f, {.range = {1, 20}, .desc = "Metres a second on foot"}};
static baker::Tweak<float> kJumpSpeed{
    "wayfarer.jump_speed", 6.0f, {.range = {0, 20}, .desc = "Metres a second up, jumping"}};
static baker::Tweak<float> kClimbMax{
    "wayfarer.climb_max", 8.0f, {.range = {0, 50}, .desc = "Metres a climb goes up"}};
static baker::Tweak<float> kClimbSpeed{
    "wayfarer.climb_speed", 2.5f, {.range = {0.5f, 10}, .desc = "Metres a second, climbing"}};
static baker::Tweak<float> kGlideSpeed{
    "wayfarer.glide_speed", 10.0f, {.range = {1, 40}, .desc = "Metres a second, gliding"}};
static baker::Tweak<float> kGlideSink{
    "wayfarer.glide_sink", 2.0f, {.range = {0.2f, 10}, .desc = "Metres a second down, gliding"}};
static baker::Tweak<float> kStep{
    "wayfarer.step", 0.5f, {.range = {0, 2}, .desc = "A step walked up without climbing"}};
static baker::Tweak<float> kPoiRadius{
    "wayfarer.poi_radius", 4.0f, {.range = {1, 20}, .desc = "Metres that count as at a point"}};
static baker::Tweak<float> kDiscoverRadius{
    "wayfarer.discover_radius",
    20.0f,
    {.range = {1, 200}, .desc = "Metres at which a point is found"}};
static baker::Tweak<int> kStreamRadius{
    "wayfarer.stream_radius",
    1,
    {.range = {0, 4}, .desc = "Cells around the player's that are built"}};

namespace {

constexpr float kRadius = 0.35f, kHalfHeight = 0.55f;  // the character's capsule
constexpr float kGravity = 9.81f;
constexpr uint64_t kClimbable = 1;  // a body's user data

wayfarer::Traversal current_rules() {
    wayfarer::Traversal t;
    t.run_speed = kRunSpeed;
    t.climb_max = kClimbMax;
    t.climb_speed = kClimbSpeed;
    t.glide_speed = kGlideSpeed;
    t.glide_sink = std::max(0.1f, float(kGlideSink));
    t.step = kStep;
    t.poi_radius = kPoiRadius;
    t.discover_radius = kDiscoverRadius;
    return t;
}

float dist(const base::NavPoint& a, const base::NavPoint& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

class WayfarerGame final : public base::Game {
public:
    void bind(baker::Context& ctx) override {
        ctx_ = &ctx;
        auto doc = [](auto f, const char* text) {
            if (f) f->doc(text);
        };
        auto poi_of = [this](baker::Symbol s) -> baker::Expected<int> {
            const int p = world_.poi(std::string(s.str()));
            if (p < 0)
                return baker::Unexpected(
                    base::not_found("no point of interest " + std::string(s.str())));
            return p;
        };
        auto mission_of = [this](baker::Symbol s) -> baker::Expected<int> {
            const int m = world_.mission(std::string(s.str()));
            if (m < 0)
                return baker::Unexpected(base::not_found("no mission " + std::string(s.str())));
            return m;
        };
        // The natives objectives are written in (§4.4, "L2").
        doc(ctx.func("at_poi",
                     [this, poi_of](baker::Symbol s) -> baker::Expected<bool> {
                         auto p = poi_of(s);
                         if (!p) return baker::Unexpected(p.error());
                         return dist(player(), world_.pois[size_t(*p)].at) <=
                                world_.rules.poi_radius;
                     }),
            "In a goal: whether the player is at a point of interest.");
        doc(ctx.func("flag",
                     [this](baker::Symbol s) -> baker::Expected<bool> {
                         const int f = world_.flag(std::string(s.str()));
                         if (f < 0)
                             return baker::Unexpected(
                                 base::not_found("no flag " + std::string(s.str())));
                         return bool(progress_.flags >> f & 1);
                     }),
            "In a goal: whether a flag is set (a point of interest sets it when visited).");
        doc(ctx.func("has_item",
                     [this](baker::Symbol s) -> baker::Expected<bool> {
                         const int i = world_.item(std::string(s.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no item " + std::string(s.str())));
                         return progress_.has(i);
                     }),
            "In a goal: whether the player holds an item.");
        doc(ctx.func("time_of_day", [this] { return std::fmod(8 + clock_ / 60, 24.0); }),
            "In a goal: the hour, 0 to 24 (play starts at 8, a game minute each second).");
        doc(ctx.func("mission.done",
                     [this, mission_of](baker::Symbol s) -> baker::Expected<bool> {
                         auto m = mission_of(s);
                         if (!m) return baker::Unexpected(m.error());
                         return progress_.done(*m);
                     }),
            "Whether a mission's objectives are all done.");
        doc(ctx.func("mission.start",
                     [this, mission_of](baker::Symbol s) -> baker::Expected<void> {
                         auto m = mission_of(s);
                         if (!m) return baker::Unexpected(m.error());
                         progress_.started |= uint64_t(1) << *m;
                         return {};
                     }),
            "Opens a mission whatever its unlock says.");
        doc(ctx.func("mission.skip_to",
                     [this, mission_of](baker::Symbol s,
                                        baker::Symbol objective) -> baker::Expected<void> {
                         auto m = mission_of(s);
                         if (!m) return baker::Unexpected(m.error());
                         const auto& os = world_.missions[size_t(*m)].objectives;
                         size_t o = 0;
                         while (o < os.size() && os[o].name != objective.str()) ++o;
                         if (o == os.size())
                             return baker::Unexpected(
                                 base::not_found("no objective " + std::string(objective.str())));
                         progress_.started |= uint64_t(1) << *m;
                         progress_.objectives[size_t(*m)] = (uint64_t(1) << o) - 1;
                         return {};
                     }),
            "Opens a mission with the objectives before this one done.");
        doc(ctx.func("mission.state",
                     [this, mission_of](baker::Symbol s) -> baker::Expected<baker::Symbol> {
                         auto m = mission_of(s);
                         if (!m) return baker::Unexpected(m.error());
                         return baker::Symbol(state_of(*m));
                     }),
            "done, open, blocked (open, but nothing open can be reached now), or locked.");
        doc(ctx.func("world.missions_done",
                     [this] {
                         int64_t n = 0;
                         for (size_t m = 0; m < world_.missions.size(); ++m)
                             n += progress_.done(int(m));
                         return n;
                     }),
            "How many missions are done.");
        doc(ctx.func("world.teleport",
                     [this, poi_of](baker::Symbol s) -> baker::Expected<void> {
                         auto p = poi_of(s);
                         if (!p) return baker::Unexpected(p.error());
                         const base::NavPoint at = world_.pois[size_t(*p)].at;
                         place(at.x, at.y + 0.2f, at.z);
                         return {};
                     }),
            "Moves the player to a point of interest.");
        doc(ctx.func("world.give",
                     [this](baker::Symbol s) -> baker::Expected<void> {
                         const int i = world_.item(std::string(s.str()));
                         if (i < 0)
                             return baker::Unexpected(
                                 base::not_found("no item " + std::string(s.str())));
                         progress_.items |= uint64_t(1) << i;
                         return {};
                     }),
            "Gives the player an item.");
        doc(ctx.func("world.autopilot",
                     [this, poi_of](baker::Symbol s) -> baker::Expected<bool> {
                         auto p = poi_of(s);
                         if (!p) return baker::Unexpected(p.error());
                         return autopilot(*p);
                     }),
            "Walks the player to a point of interest by the path the analyses use, with the "
            "abilities it holds; false if there is none.");
        doc(ctx.func("world.arrived", [this] { return autopilot_ < 0 && autopilot_arrived_; }),
            "Whether the last autopilot reached its point.");
        doc(ctx.func("world.cells_built", [this] { return int64_t(built_count()); }),
            "Cells built now (physics and navigation), around the player.");
        doc(ctx.func("world.cells_peak", [this] { return int64_t(peak_built_); }),
            "The most cells built at once since the world loaded.");
        doc(ctx.func("world.cells", [this] { return int64_t(world_.cells.size()); }),
            "Cells in the world.");
        doc(ctx.func("world.prove",
                     [this] {
                         prepare();
                         proof_ = wayfarer::prove(world_, nav_);
                         proved_ = true;
                         return int64_t(proof_.softlocked_states);
                     }),
            "Searches every save point and proves each mission completable from it; returns "
            "the softlocked save points. The words below read it.");
        doc(ctx.func("world.softlocks", [this] { return int64_t(proof_.softlocked_states); }),
            "Save points from which some mission can no longer be finished (after world.prove).");
        doc(ctx.func("world.unreachable", [this] { return int64_t(proof_.unreachable.size()); }),
            "Objectives never done in any save point (after world.prove).");
        doc(ctx.func("world.unreachable_pois",
                     [this] { return int64_t(proof_.unreachable_pois.size()); }),
            "Points of interest the player can never get to (after world.prove).");
        doc(ctx.func("world.unanalysed", [this] { return int64_t(proof_.unanalysed.size()); }),
            "Goals the proof cannot read: not at_poi, flag, has_item, or mission.done.");
        doc(ctx.func("world.completable", [this] { return proof_.complete; }),
            "Whether every mission can be finished from the start (after world.prove).");
        doc(ctx.func("world.proof_report", [this] { return proof_report(); }),
            "The last proof as a person reads it.");
        doc(ctx.func("world.pacing",
                     [this](baker::Symbol style, double seconds) -> baker::Expected<double> {
                         wayfarer::Style st;
                         if (!wayfarer::parse_style(std::string(style.str()), st))
                             return baker::Unexpected(
                                 base::not_found("no bot style " + std::string(style.str()) +
                                                 "; direct, curious, or completionist"));
                         prepare();
                         pacing_ = wayfarer::explore(world_, nav_, st, float(seconds), seed_);
                         pacing_style_ = std::string(style.str());
                         return double(pacing_.longest_gap);
                     }),
            "A bot of a style (direct, curious, completionist) explores for seconds, seeded "
            "from $seed; returns the longest stretch without a discovery, in seconds.");
        doc(ctx.func("world.pacing_gap", [this] { return double(pacing_.longest_gap); }),
            "The last bot's longest stretch without a discovery, in seconds.");
        doc(ctx.func("world.pacing_missed", [this] { return int64_t(pacing_.missed.size()); }),
            "Points of interest the last bot never found.");
        doc(ctx.func("world.pacing_found", [this] { return int64_t(pacing_.found); }),
            "Points of interest the last bot found.");
        doc(ctx.func("world.pacing_done", [this] { return double(pacing_.all_done); }),
            "When the last bot had finished every mission, in seconds; -1 if it did not.");
        doc(ctx.func("world.pacing_report", [this] { return pacing_report(); }),
            "The last bot's run as a person reads it: discoveries and missions, in time.");
        doc(ctx.func("world.glide_margin",
                     [this] {
                         world_.rules = current_rules();
                         const auto m = wayfarer::glide_margins(world_);
                         return m.empty() ? 0.0 : double(*std::min_element(m.begin(), m.end()));
                     }),
            "The tightest glide's margin: the glide ratio over the ratio it needs.");
        doc(ctx.func("world.nav_ms", [this] { return nav_ms_; }),
            "Milliseconds the last streamed cell's navigation tile took to build.");
    }

    void unbind(baker::Context&) override {
        character_ = nullptr;
        physics_.reset();
        ctx_ = nullptr;
    }

    void load(const baker::Config& cfg) override {
        wayfarer::World next;
        std::string error;
        if (!wayfarer::load(cfg.root(), next, error)) {
            std::fprintf(stderr, "wayfarer: %s; keeping the last good world\n", error.c_str());
            return;
        }
        // A cell whose source changed is rebuilt where it stands; the rest stay built.
        std::vector<uint64_t> old_versions(next.cells.size(), 0);
        for (size_t c = 0; c < next.cells.size() && c < world_.cells.size(); ++c)
            if (world_.cells[c].name == next.cells[c].name)
                old_versions[c] = world_.cells[c].version;
        const bool first = !loaded_;
        if (!first)
            for (size_t c = 0; c < built_.size(); ++c)
                if (c >= next.cells.size() || old_versions[c] != next.cells[c].version) unbuild(c);
        world_ = std::move(next);
        loaded_ = true;
        built_.resize(world_.cells.size());
        proved_ = false;
        if (first || progress_.objectives.size() != world_.missions.size() ||
            progress_.found.size() != world_.pois.size())
            reset(seed_);
    }

    void reset(uint64_t seed) override {
        seed_ = seed;
        if (!loaded_) return;
        progress_.reset(world_);
        clock_ = 0;
        autopilot_ = -1;
        autopilot_arrived_ = false;
        open_since_.assign(world_.missions.size(), {});
        if (!physics_) {
            physics_ = std::make_unique<base::Physics>();
            built_.assign(world_.cells.size(), {});
        }
        place(world_.spawn.x, world_.spawn.y + 0.2f, world_.spawn.z);
        stream();
        director();
    }

    bool start(base::App&) override {
        if (!loaded_) std::fprintf(stderr, "wayfarer: the data has no usable world\n");
        return loaded_;
    }

    void step(double dt_d) override {
        if (!character_) return;
        world_.rules = current_rules();
        const float dt = float(dt_d);
        clock_ += dt_d;
        move(dt);
        stream();
        director();
    }

    void event(const SDL_Event& e) override {
        const bool keys_taken = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
        const bool mouse_taken = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
        if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && !keys_taken) {
            const bool down = e.type == SDL_EVENT_KEY_DOWN;
            switch (e.key.key) {
                case SDLK_W: keys_[0] = down; break;
                case SDLK_S: keys_[1] = down; break;
                case SDLK_A: keys_[2] = down; break;
                case SDLK_D: keys_[3] = down; break;
                case SDLK_Q: keys_[4] = down; break;
                case SDLK_E: keys_[5] = down; break;
                case SDLK_SPACE: keys_[6] = down; break;
                default: break;
            }
            if (down) autopilot_ = -1;  // the player takes over
        } else if (e.type == SDL_EVENT_MOUSE_MOTION && !mouse_taken &&
                   (e.motion.state & SDL_BUTTON_RMASK)) {
            yaw_ -= e.motion.xrel * 0.005f;
            pitch_ = std::clamp(pitch_ + e.motion.yrel * 0.004f, -0.2f, 1.2f);
        }
    }

    void draw(base::Renderer& r, float) override {
        r.clear_color(0.58f, 0.74f, 0.9f);
        if (!character_) return;
        const base::NavPoint p = player();
        base::Camera cam;
        const float back = 9, up = 3.5f + pitch_ * 6;
        cam.eye[0] = p.x - std::sin(yaw_) * back;
        cam.eye[1] = p.y + up;
        cam.eye[2] = p.z - std::cos(yaw_) * back;
        cam.at[0] = p.x;
        cam.at[1] = p.y + 1.4f;
        cam.at[2] = p.z;
        cam.far_plane = 400;
        r.camera(cam);
        for (size_t c = 0; c < world_.cells.size(); ++c) {
            const wayfarer::Cell& cell = world_.cells[c];
            const bool built = !built_[c].empty();
            const float half = world_.cell_size / 2;
            const float g = built ? 1.0f : 0.75f;
            r.box(float(cell.cx) * world_.cell_size + half, -0.25f,
                  float(cell.cz) * world_.cell_size + half, world_.cell_size, 0.5f,
                  world_.cell_size, 0.42f * g, 0.62f * g, 0.36f * g);
            if (!built) continue;
            for (const wayfarer::Block& b : cell.blocks)
                r.box(b.x, b.y + b.h / 2, b.z, b.w, b.h, b.d, b.climb ? 0.62f : 0.6f,
                      b.climb ? 0.48f : 0.6f, b.climb ? 0.34f : 0.62f);
        }
        for (size_t i = 0; i < world_.pois.size(); ++i) {
            const wayfarer::Poi& q = world_.pois[i];
            const bool target = is_target(int(i));
            const float h = target ? 6.0f : 2.5f;
            float c[3] = {0.95f, 0.95f, 0.95f};
            if (q.landmark) c[0] = 0.75f, c[1] = 0.75f, c[2] = 0.8f;
            if (target) c[0] = 1, c[1] = 0.8f, c[2] = 0.15f;
            if (!progress_.found[i]) c[0] *= 0.5f, c[1] *= 0.5f, c[2] *= 0.5f;
            r.box(q.at.x, q.at.y + h / 2, q.at.z, 0.4f, h, 0.4f, c[0], c[1], c[2]);
        }
        const float tint = climbing_ ? 0.6f : 1.0f;
        r.box(p.x, p.y + 0.9f, p.z, 0.7f, 1.8f, 0.7f, 0.9f * tint, 0.3f, 0.25f);
        if (gliding_) r.box(p.x, p.y + 2.0f, p.z, 3.2f, 0.08f, 1.2f, 0.95f, 0.9f, 0.7f);
        r.text(2, 3, "missions %d/%zu   cells built %d/%zu   %s%s", int(missions_done()),
               world_.missions.size(), built_count(), world_.cells.size(),
               climbing_  ? "climbing"
               : gliding_ ? "gliding"
                          : "",
               autopilot_ >= 0 ? "   autopilot" : "");
        if (!hint_.empty()) r.text(2, 4, "hint: %s", hint_.c_str());
    }

    void ui() override {
        if (!loaded_) return;
        ImGui::SetNextWindowPos(ImVec2(900, 40), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 640), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Wayfarer")) {
            ImGui::End();
            return;
        }
        map();
        ImGui::Separator();
        // The gating view: every mission, coloured by whether it can be reached from here.
        for (size_t m = 0; m < world_.missions.size(); ++m) {
            const std::string s = state_of(int(m));
            const ImVec4 colour = s == "done"      ? ImVec4(0.6f, 0.6f, 0.6f, 1)
                                  : s == "open"    ? ImVec4(0.4f, 0.9f, 0.4f, 1)
                                  : s == "blocked" ? ImVec4(0.95f, 0.6f, 0.2f, 1)
                                                   : ImVec4(0.5f, 0.5f, 0.7f, 1);
            ImGui::TextColored(colour, "%-18s %s", world_.missions[m].name.c_str(), s.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button("Prove")) {
            prepare();
            proof_ = wayfarer::prove(world_, nav_);
            proved_ = true;
        }
        ImGui::SameLine();
        for (const char* style : {"direct", "curious", "completionist"}) {
            if (ImGui::Button(style)) {
                prepare();
                wayfarer::Style st;
                (void)wayfarer::parse_style(style, st);
                pacing_ = wayfarer::explore(world_, nav_, st, 1800, seed_);
                pacing_style_ = style;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (proved_) ImGui::TextWrapped("%s", proof_report().c_str());
        if (!pacing_style_.empty()) ImGui::TextWrapped("%s", pacing_report().c_str());
        ImGui::End();
    }

private:
    // --- The character ---

    base::NavPoint player() const {
        if (!character_) return world_.spawn;
        const JPH::RVec3 p = character_->GetPosition();
        return {float(p.GetX()), float(p.GetY()), float(p.GetZ())};
    }

    void place(float x, float y, float z) {
        if (!physics_) return;
        if (!character_) {
            JPH::CharacterVirtualSettings s;
            s.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
            s.mShape = JPH::RotatedTranslatedShapeSettings(
                           JPH::Vec3(0, kHalfHeight + kRadius, 0), JPH::Quat::sIdentity(),
                           new JPH::CapsuleShape(kHalfHeight, kRadius))
                           .Create()
                           .Get();
            s.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -kRadius);
            character_ = new JPH::CharacterVirtual(&s, JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
                                                   &physics_->system());
        }
        character_->SetPosition(JPH::RVec3(x, y, z));
        character_->SetLinearVelocity(JPH::Vec3::sZero());
        climbing_ = gliding_ = false;
        climb_time_ = mantle_ = 0;
        stream();
    }

    void move(float dt) {
        const wayfarer::Traversal& r = world_.rules;
        const bool climb_ok = (world_.abilities_of(progress_.items) & base::nav::kClimb) != 0;
        const bool glide_ok = (world_.abilities_of(progress_.items) & base::nav::kGlide) != 0;
        // What the player (or the autopilot) asks for: a direction on the ground, jump, glide.
        float wx = 0, wz = 0;
        bool jump = false, glide = false;
        if (autopilot_ >= 0) {
            steer(wx, wz, jump, glide);
        } else {
            if (keys_[4]) yaw_ += 1.8f * dt;
            if (keys_[5]) yaw_ -= 1.8f * dt;
            const float f = float(keys_[0]) - float(keys_[1]),
                        s = float(keys_[2]) - float(keys_[3]);
            const float fx = std::sin(yaw_), fz = std::cos(yaw_);
            wx = f * fx + s * fz;
            wz = f * fz - s * fx;
            jump = glide = keys_[6];
        }
        const float len = std::sqrt(wx * wx + wz * wz);
        if (len > 1) wx /= len, wz /= len;
        JPH::Vec3 v = character_->GetLinearVelocity();
        const bool supported = character_->IsSupported();
        // Against a climbable face, pushing into it, with the gloves: climb, up to climb_max.
        bool at_wall = false;
        for (const auto& c : character_->GetActiveContacts()) {
            if (!c.mHadCollision || c.mUserData != kClimbable) continue;
            const JPH::Vec3 n = c.mContactNormal;
            if (std::abs(n.GetY()) < 0.3f && -(n.GetX() * wx + n.GetZ() * wz) > 0.4f)
                at_wall = true;
        }
        if (climb_ok && at_wall && climb_time_ < r.climb_max / r.climb_speed) {
            climbing_ = true;
            climb_time_ += dt;
            v = JPH::Vec3(wx * 0.5f, r.climb_speed, wz * 0.5f);
        } else {
            if (climbing_ && !at_wall) mantle_ = 0.35f;  // over the top: a last push up
            climbing_ = false;
            if (supported) climb_time_ = 0;
            const float speed = r.run_speed;
            JPH::Vec3 h(wx * speed, 0, wz * speed);
            if (mantle_ > 0) {
                mantle_ -= dt;
                v = JPH::Vec3(wx * speed, r.climb_speed, wz * speed);
            } else if (supported &&
                       character_->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround) {
                v = h + JPH::Vec3(0, jump && !jump_held_ ? float(kJumpSpeed) : 0, 0);
                gliding_ = false;
            } else {
                v = JPH::Vec3(h.GetX(), v.GetY() - kGravity * dt, h.GetZ());
                gliding_ = glide && glide_ok && v.GetY() < 0;
                if (gliding_) {
                    const float gx = len > 0.1f ? wx : std::sin(yaw_),
                                gz = len > 0.1f ? wz : std::cos(yaw_);
                    const float gl = std::max(0.001f, std::sqrt(gx * gx + gz * gz));
                    v = JPH::Vec3(gx / gl * r.glide_speed, std::max(v.GetY(), -r.glide_sink),
                                  gz / gl * r.glide_speed);
                }
            }
        }
        jump_held_ = jump;
        character_->SetLinearVelocity(v);
        JPH::CharacterVirtual::ExtendedUpdateSettings u;
        u.mWalkStairsStepUp = JPH::Vec3(0, r.step, 0);
        JPH::PhysicsSystem& sys = physics_->system();
        character_->ExtendedUpdate(
            dt, JPH::Vec3(0, -kGravity, 0), u,
            JPH::DefaultBroadPhaseLayerFilter(sys.GetObjectVsBroadPhaseLayerFilter(),
                                              base::layers::kMoving),
            JPH::DefaultObjectLayerFilter(sys.GetObjectLayerPairFilter(), base::layers::kMoving),
            JPH::BodyFilter(), JPH::ShapeFilter(), physics_->temp());
        // Fallen out of the world: back to the spawn.
        if (character_->GetPosition().GetY() < -20) place(world_.spawn.x, 0.2f, world_.spawn.z);
    }

    // --- The autopilot: the analyses' path, walked by the game's own character ---

    bool autopilot(int poi) {
        prepare();
        autopilot_arrived_ = false;
        path_.clear();
        const base::NavPoint at = player();
        if (!nav_.path(at, world_.pois[size_t(poi)].at, path_,
                       world_.abilities_of(progress_.items)) &&
            (path_.empty() ||
             dist(path_.back(), world_.pois[size_t(poi)].at) > world_.rules.poi_radius)) {
            autopilot_ = -1;
            return false;
        }
        autopilot_ = poi;
        waypoint_ = 1;
        stall_ = 0;
        best_ = 1e30f;
        return true;
    }

    void steer(float& wx, float& wz, bool& jump, bool& glide) {
        const base::NavPoint p = player();
        const wayfarer::Poi& target = world_.pois[size_t(autopilot_)];
        if (dist(p, target.at) <= world_.rules.poi_radius * 0.75f) {
            autopilot_arrived_ = true;
            autopilot_ = -1;
            return;
        }
        while (waypoint_ < path_.size()) {
            const base::NavPoint& w = path_[waypoint_];
            const float flat = std::hypot(w.x - p.x, w.z - p.z);
            if (flat < 0.8f && std::abs(w.y - p.y) < 1.2f)
                ++waypoint_;
            else
                break;
        }
        const base::NavPoint w = waypoint_ < path_.size() ? path_[waypoint_] : target.at;
        float dx = w.x - p.x, dz = w.z - p.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > 0.01f) dx /= d, dz /= d;
        wx = dx;
        wz = dz;
        yaw_ = std::atan2(dx, dz);
        glide = true;  // in the air, the autopilot glides if it can
        jump = false;
        // Not getting closer for five seconds: plan again from here.
        const float left = dist(p, target.at);
        if (left < best_ - 0.5f)
            best_ = left, stall_ = 0;
        else
            stall_ += 1.0f / 60;
        if (stall_ > 5) {
            const int poi = autopilot_;
            if (!autopilot(poi)) autopilot_ = -1;
        }
    }

    // --- Streaming ---

    int built_count() const {
        int n = 0;
        for (const auto& b : built_) n += !b.empty();
        return n;
    }

    void stream() {
        if (!physics_) return;
        const base::NavPoint p = player();
        const int pcx = int(std::floor(p.x / world_.cell_size)),
                  pcz = int(std::floor(p.z / world_.cell_size));
        const int radius = kStreamRadius;
        for (size_t c = 0; c < world_.cells.size(); ++c) {
            const wayfarer::Cell& cell = world_.cells[c];
            const int d = std::max(std::abs(cell.cx - pcx), std::abs(cell.cz - pcz));
            if (d <= radius && built_[c].empty())
                build(c);
            else if (d > radius + 1 && !built_[c].empty())
                unbuild(c);  // one cell of give
        }
        peak_built_ = std::max(peak_built_, built_count());
    }

    void build(size_t c) {
        JPH::BodyInterface& bi = physics_->bodies();
        const wayfarer::Cell& cell = world_.cells[c];
        const float half = world_.cell_size / 2;
        auto add = [&](float x, float y, float z, float hx, float hy, float hz, bool climb) {
            JPH::BodyCreationSettings s(new JPH::BoxShape(JPH::Vec3(hx, hy, hz), 0.0f),
                                        JPH::RVec3(x, y, z), JPH::Quat::sIdentity(),
                                        JPH::EMotionType::Static, base::layers::kStatic);
            s.mUserData = climb ? kClimbable : 0;
            built_[c].push_back(bi.CreateAndAddBody(s, JPH::EActivation::DontActivate));
        };
        add(float(cell.cx) * world_.cell_size + half, -0.25f,
            float(cell.cz) * world_.cell_size + half, half, 0.25f, half, false);
        for (const wayfarer::Block& b : cell.blocks)
            add(b.x, b.y + b.h / 2, b.z, b.w / 2, b.h / 2, b.d / 2, b.climb);
        // Its navigation tile streams with it (the analyses keep their own, whole).
        const auto t0 = std::chrono::steady_clock::now();
        if (!streamed_nav_ready_)
            streamed_nav_ready_ = streamed_nav_.init(wayfarer::nav_settings(world_));
        (void)streamed_nav_.add_tile(cell.cx, cell.cz, wayfarer::geometry(world_, int(c)));
        nav_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                      .count();
    }

    void unbuild(size_t c) {
        if (!physics_ || c >= built_.size()) return;
        JPH::BodyInterface& bi = physics_->bodies();
        for (const JPH::BodyID id : built_[c]) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
        built_[c].clear();
        if (c < world_.cells.size())
            streamed_nav_.remove_tile(world_.cells[c].cx, world_.cells[c].cz);
    }

    // --- The director ---

    void director() {
        const base::NavPoint p = player();
        const wayfarer::Traversal& r = world_.rules;
        for (size_t i = 0; i < world_.pois.size(); ++i) {
            const float d = dist(p, world_.pois[i].at);
            if (d <= r.discover_radius) progress_.found[i] = true;
            if (d <= r.poi_radius) wayfarer::take(world_, progress_, int(i));
        }
        // Each open objective's goal, as written, evaluated in script.
        for (bool changed = true; changed;) {
            changed = false;
            for (size_t m = 0; m < world_.missions.size(); ++m) {
                const wayfarer::Mission& mi = world_.missions[m];
                if (progress_.done(int(m))) continue;
                if (!(progress_.started >> m & 1)) {
                    bool open = true;
                    for (const wayfarer::Goal& g : mi.unlock) open = open && truthy(g.command);
                    if (!open) continue;
                    progress_.started |= uint64_t(1) << m;
                }
                for (size_t o = 0; o < mi.objectives.size(); ++o) {
                    if (!progress_.open(world_, int(m), int(o))) continue;
                    if (open_since_[m].size() < mi.objectives.size())
                        open_since_[m].assign(mi.objectives.size(), -1);
                    if (open_since_[m][o] < 0) open_since_[m][o] = clock_;
                    if (truthy(mi.objectives[o].goal.command)) {
                        progress_.objectives[m] |= uint64_t(1) << o;
                        changed = true;
                    }
                }
                const uint64_t all = (uint64_t(1) << mi.objectives.size()) - 1;
                if (progress_.objectives[m] == all) {
                    progress_.missions |= uint64_t(1) << m;
                    for (int it : mi.reward_items) progress_.items |= uint64_t(1) << it;
                    changed = true;
                }
            }
        }
        // A hint for the first open objective that has waited past its hint_after.
        hint_.clear();
        for (size_t m = 0; m < world_.missions.size() && hint_.empty(); ++m)
            for (size_t o = 0; o < world_.missions[m].objectives.size(); ++o) {
                const wayfarer::Objective& ob = world_.missions[m].objectives[o];
                if (!progress_.open(world_, int(m), int(o)) || ob.hint_after < 0) continue;
                if (o < open_since_[m].size() && open_since_[m][o] >= 0 &&
                    clock_ - open_since_[m][o] > ob.hint_after) {
                    hint_ = world_.missions[m].name + ": " + ob.name;
                    break;
                }
            }
    }

    bool truthy(const baker::Node& command) {
        if (!ctx_ || command.type() != baker::Type::Command) return false;
        auto r = ctx_->eval(command);
        if (!r) return false;
        return base::truthy(*r);
    }

    size_t missions_done() const {
        size_t n = 0;
        for (size_t m = 0; m < world_.missions.size(); ++m) n += progress_.done(int(m));
        return n;
    }

    // Whether a poi is where an open objective points.
    bool is_target(int poi) const {
        for (size_t m = 0; m < world_.missions.size(); ++m)
            for (size_t o = 0; o < world_.missions[m].objectives.size(); ++o) {
                if (!progress_.open(world_, int(m), int(o))) continue;
                const wayfarer::Goal& g = world_.missions[m].objectives[o].goal;
                if (g.fact == wayfarer::Fact::AtPoi && g.target == poi) return true;
                if (g.fact == wayfarer::Fact::Flag && world_.pois[size_t(poi)].sets == g.target)
                    return true;
                if (g.fact == wayfarer::Fact::HasItem)
                    for (int it : world_.pois[size_t(poi)].gives)
                        if (it == g.target) return true;
            }
        return false;
    }

    std::string state_of(int m) {
        if (progress_.done(m)) return "done";
        if (!progress_.unlocked(world_, m)) return "locked";
        // Open: can anything open in it be reached from here, with what is held?
        prepare();
        const uint16_t ab = world_.abilities_of(progress_.items);
        for (size_t o = 0; o < world_.missions[size_t(m)].objectives.size(); ++o) {
            if (!progress_.open(world_, m, int(o))) continue;
            for (size_t i = 0; i < world_.pois.size(); ++i)
                if (is_target(int(i)) && nav_.distance(player(), world_.pois[i].at, ab) >= 0)
                    return "open";
        }
        return "blocked";
    }

    // --- The analyses' navigation: every cell, built once per load ---

    void prepare() {
        world_.rules = current_rules();
        // Built again only when the geometry it depends on changes: a reload that tunes a
        // speed keeps it (it took 174 ms a reload before, D114), and a tuned climb_max
        // rebuilds it.
        const uint64_t key = wayfarer::nav_key(world_);
        if (key == nav_key_) return;
        std::string error;
        if (!wayfarer::build_nav(world_, nav_, &error))
            std::fprintf(stderr, "wayfarer: %s\n", error.c_str());
        nav_key_ = key;
    }

    std::string proof_report() const {
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "%d save points: %d softlocked; %zu objectives unreachable; %zu points "
                      "never reached; %zu goals unanalysed; %s",
                      proof_.states, proof_.softlocked_states, proof_.unreachable.size(),
                      proof_.unreachable_pois.size(), proof_.unanalysed.size(),
                      proof_.complete ? "every mission completable from the start"
                                      : "NOT every mission completable from the start");
        std::string s = buf;
        for (const auto& l : proof_.softlocks) s += "; softlock " + l.where + ": " + l.why;
        for (const auto& u : proof_.unreachable) s += "; unreachable " + u;
        for (const auto& u : proof_.unanalysed) s += "; unanalysed " + u;
        for (const auto& u : proof_.unreachable_pois) s += "; never reached " + u;
        return s;
    }

    std::string pacing_report() const {
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "%s bot: found %d of %zu points; longest stretch without a discovery %.0f s "
                      "(from %.0f s); every mission done at %.0f s",
                      pacing_style_.c_str(), pacing_.found, world_.pois.size(),
                      double(pacing_.longest_gap), double(pacing_.longest_gap_at),
                      double(pacing_.all_done));
        std::string s = buf;
        for (int i : pacing_.missed) s += "; missed " + world_.pois[size_t(i)].name;
        for (size_t m = 0; m < world_.missions.size() && m < pacing_.mission_done.size(); ++m) {
            std::snprintf(buf, sizeof buf, "; %s %.0f s", world_.missions[m].name.c_str(),
                          double(pacing_.mission_done[m]));
            s += buf;
        }
        return s;
    }

    // --- The map: cells, blocks, points, the mission graph, and the player ---

    void map() {
        const float size = 300;
        const float span =
            std::max(float(world_.cells_x), float(world_.cells_z)) * world_.cell_size;
        const float k = size / span;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // North (+z) up.
        auto at = [&](float x, float z) { return ImVec2(o.x + x * k, o.y + size - z * k); };
        for (size_t c = 0; c < world_.cells.size(); ++c) {
            const wayfarer::Cell& cell = world_.cells[c];
            const float x0 = float(cell.cx) * world_.cell_size,
                        z0 = float(cell.cz) * world_.cell_size;
            dl->AddRectFilled(
                at(x0, z0 + world_.cell_size), at(x0 + world_.cell_size, z0),
                built_[c].empty() ? IM_COL32(50, 70, 45, 255) : IM_COL32(70, 105, 60, 255));
            dl->AddRect(at(x0, z0 + world_.cell_size), at(x0 + world_.cell_size, z0),
                        IM_COL32(30, 40, 30, 255));
            for (const wayfarer::Block& b : cell.blocks) {
                const int shade = int(std::min(255.0f, 110 + (b.y + b.h) * 7));
                dl->AddRectFilled(at(b.x - b.w / 2, b.z + b.d / 2),
                                  at(b.x + b.w / 2, b.z - b.d / 2),
                                  b.climb ? IM_COL32(shade, shade * 3 / 4, shade / 2, 255)
                                          : IM_COL32(shade, shade, shade, 255));
            }
            for (const wayfarer::Move& m : cell.moves)
                dl->AddLine(at(m.from.x, m.from.z), at(m.to.x, m.to.z),
                            m.glide ? IM_COL32(120, 200, 255, 255) : IM_COL32(200, 200, 120, 255),
                            2);
        }
        // The mission graph: each objective's point, linked in order, per mission.
        for (const wayfarer::Mission& m : world_.missions) {
            const wayfarer::Poi* prev = nullptr;
            for (const wayfarer::Objective& ob : m.objectives) {
                if (ob.goal.fact != wayfarer::Fact::AtPoi) continue;
                const wayfarer::Poi& q = world_.pois[size_t(ob.goal.target)];
                if (prev)
                    dl->AddLine(at(prev->at.x, prev->at.z), at(q.at.x, q.at.z),
                                IM_COL32(255, 210, 60, 200), 1.5f);
                prev = &q;
            }
        }
        for (size_t i = 0; i < world_.pois.size(); ++i) {
            const wayfarer::Poi& q = world_.pois[i];
            const ImU32 c = is_target(int(i)) ? IM_COL32(255, 210, 40, 255)
                            : q.landmark      ? IM_COL32(190, 190, 210, 255)
                                              : IM_COL32(255, 255, 255, 255);
            if (progress_.found[i])
                dl->AddCircleFilled(at(q.at.x, q.at.z), 3.5f, c);
            else
                dl->AddCircle(at(q.at.x, q.at.z), 3.5f, c);
        }
        const base::NavPoint p = player();
        const ImVec2 pp = at(p.x, p.z);
        const float s = std::sin(yaw_), c = std::cos(yaw_);
        dl->AddTriangleFilled(ImVec2(pp.x + s * 7, pp.y - c * 7),
                              ImVec2(pp.x - c * 4, pp.y - s * 4),
                              ImVec2(pp.x + c * 4, pp.y + s * 4), IM_COL32(230, 60, 50, 255));
        ImGui::Dummy(ImVec2(size, size));
    }

    baker::Context* ctx_ = nullptr;
    wayfarer::World world_;
    bool loaded_ = false;
    wayfarer::Progress progress_;
    std::vector<std::vector<double>> open_since_;  // per mission, per objective: when it opened
    std::string hint_;
    double clock_ = 0;
    uint64_t seed_ = 0;
    // Physics, and the cells built in it.
    std::unique_ptr<base::Physics> physics_;
    JPH::Ref<JPH::CharacterVirtual> character_;
    std::vector<std::vector<JPH::BodyID>> built_;
    int peak_built_ = 0;
    base::NavMesh streamed_nav_;
    bool streamed_nav_ready_ = false;
    double nav_ms_ = 0;
    // The character's state.
    float yaw_ = 0.6f, pitch_ = 0.3f;
    bool keys_[7] = {};
    bool climbing_ = false, gliding_ = false, jump_held_ = false;
    float climb_time_ = 0, mantle_ = 0;
    // The autopilot.
    int autopilot_ = -1;
    bool autopilot_arrived_ = false;
    std::vector<base::NavPoint> path_;
    size_t waypoint_ = 0;
    float stall_ = 0, best_ = 0;
    // The analyses.
    base::NavMesh nav_;
    uint64_t nav_key_ = 0;  // what nav_ was built for; 0: nothing yet
    bool proved_ = false;
    wayfarer::Proof proof_;
    wayfarer::Pacing pacing_;
    std::string pacing_style_;
};

}  // namespace

int main(int argc, char** argv) {
    WayfarerGame game;
    return base::main(game, argc, argv, {.title = "Wayfarer", .data_dir = WAYFARER_DATA});
}
