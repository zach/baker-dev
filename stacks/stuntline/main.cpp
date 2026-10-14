// Stuntline (game-stacks.md §4.5): stunt races on tracks laid from a kit. The races are
// race.hpp; this file is the game around them: driving with a pad or keys, the chase camera,
// the script words (track.), the workbench, and drawing.
//
//   stacks_stuntline                        play: drive the first track
//   stacks_stuntline sim --seeds 1-5        each track: valid, finishes, crashes, airtime, jumps
//   stacks_stuntline eval "{track.load canyon_jump}" "{track.report}"
//
// Pad: right trigger throttle, left trigger brake, left stick steer, A handbrake. Keys: W or
// up, S or down, A and D or left and right, space handbrake. R puts the car back; Tab swaps
// you for an AI driver; N changes track.
#include "race.hpp"

#include <base/app.hpp>
#include <base/render.hpp>
#include <base/words.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <moira/std_interop.hpp>
#include <moira/std_script.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

static moira::Tweak<float> kChaseDistance{
    "stuntline.chase_distance", 9.0f, {.range = {2, 40}, .desc = "Metres behind the car"}};
static moira::Tweak<float> kChaseHeight{
    "stuntline.chase_height", 3.5f, {.range = {0, 20}, .desc = "Metres above the car"}};
static moira::Tweak<float> kPatience{
    "stuntline.patience",
    4.0f,
    {.range = {1, 20}, .desc = "An analysis gives up at par times this"}};

namespace {

class StuntlineGame final : public base::Game {
public:
    void bind(moira::Context& ctx) override {
        auto doc = [](auto f, const char* text) {
            if (f) f->doc(text);
        };
        doc(ctx.func("track.load",
                     [this](moira::Symbol name) -> moira::Expected<void> {
                         const int t = kit_.track(std::string(name.str()));
                         if (t < 0)
                             return moira::Unexpected(
                                 base::not_found("no track " + std::string(name.str())));
                         select(t);
                         return {};
                     }),
            "Opens a track of (tracks ...) and puts the car at its start.");
        doc(ctx.func("track.name", [this] { return moira::Symbol(track().name); }),
            "The open track.");
        doc(ctx.func("track.valid", [this] { return layout_.connected; }),
            "Whether every piece's in matches the out of the piece before it.");
        doc(ctx.func("track.par", [this] { return double(track().par); }),
            "The track's par, in seconds.");
        doc(ctx.func("track.drive",
                     [this](moira::Symbol who) -> moira::Expected<void> {
                         const std::string name(who.str());
                         if (name != "human" && kit_.driver(name) < 0)
                             return moira::Unexpected(base::not_found(
                                 "no driver " + name + ": human, or one of (drivers ...)"));
                         driver_ = name == "human" ? -1 : kit_.driver(name);
                         restart();
                         return {};
                     }),
            "Who drives the game's car: human, or a driver of (drivers ...); restarts the race.");
        doc(ctx.func("track.finished", [this] { return race_ && race_->finished(); }),
            "Whether the game's car has crossed the line.");
        doc(ctx.func("track.time", [this] { return race_ ? double(race_->seconds()) : 0.0; }),
            "The game's race time, in seconds.");
        doc(ctx.func("track.crashes", [this] { return int64_t(race_ ? race_->crashes : 0); }),
            "Crashes in the game's race.");
        doc(ctx.func("track.finish_time",
                     [this](moira::Symbol d) -> moira::Expected<double> {
                         auto r = analysis(d);
                         if (!r) return moira::Unexpected(r.error());
                         return (*r)->finished ? double((*r)->seconds) : -1.0;
                     }),
            "A driver's time over the open track, headless; -1 if it did not finish in time.");
        doc(ctx.func("track.crash_rate",
                     [this](moira::Symbol d) -> moira::Expected<double> {
                         auto r = analysis(d);
                         if (!r) return moira::Unexpected(r.error());
                         return double((*r)->crashes) / double(track().lay.size());
                     }),
            "A driver's crashes per piece of the open track.");
        doc(ctx.func("track.airtime_share",
                     [this](moira::Symbol d) -> moira::Expected<double> {
                         auto r = analysis(d);
                         if (!r) return moira::Unexpected(r.error());
                         return (*r)->seconds > 0 ? double((*r)->airtime / (*r)->seconds) : 0.0;
                     }),
            "The share of a driver's race spent with no wheel on the road.");
        doc(ctx.func("track.skill_spread",
                     [this]() -> moira::Expected<double> {
                         auto slow = analysis(moira::Symbol("rookie"));
                         auto fast = analysis(moira::Symbol("ace"));
                         if (!slow) return moira::Unexpected(slow.error());
                         if (!fast) return moira::Unexpected(fast.error());
                         const float limit = track().par * kPatience;
                         const float a = (*slow)->finished ? (*slow)->seconds : limit;
                         const float b = (*fast)->finished ? (*fast)->seconds : limit;
                         return double(a / std::max(0.1f, b));
                     }),
            "The rookie's time over the ace's: how much skill the track rewards.");
        doc(ctx.func("track.jump_commitment",
                     [this]() -> moira::Expected<int64_t> {
                         auto pro = analysis(moira::Symbol("pro"));
                         if (!pro) return moira::Unexpected(pro.error());
                         const auto& speeds = (*pro)->gap_arrival;
                         std::vector<float> slow(speeds);
                         for (float& v : slow) v *= 0.7f;
                         const auto full = stuntline::gap_margins(kit_, track(), speeds);
                         const auto weak = stuntline::gap_margins(kit_, track(), slow);
                         for (size_t i = 0; i < full.size(); ++i)
                             if (i >= speeds.size() || full[i] < 0.85f || weak[i] >= 1) return 0;
                         return 1;
                     }),
            "1 if the pro clears every jump at the speed it arrives with, and 70% of that would "
            "not: a jump that needs commitment. (Ballistic, back to the lip's height.)");
        doc(ctx.func("track.report", [this] { return report(); }),
            "Every driver over the open track: time, crashes and where, airtime, jump speeds.");
    }

    void load(const moira::Config& cfg) override {
        stuntline::Kit next;
        std::string error;
        if (!stuntline::load(*cfg.root(), next, error)) {
            std::fprintf(stderr, "stuntline: %s; keeping the last good kit\n", error.c_str());
            return;
        }
        const std::string open = loaded_ ? track().name : std::string();
        kit_ = std::move(next);
        loaded_ = true;
        const int t = kit_.track(open);
        if (driver_ >= int(kit_.drivers.size())) driver_ = -1;
        select(t >= 0 ? t : 0);
    }

    void reset(uint64_t seed) override {
        seed_ = seed;
        driver_ = -1;
        if (loaded_) select(track_);
    }

    bool start(base::App&) override {
        if (!loaded_) std::fprintf(stderr, "stuntline: the data has no usable track\n");
        return loaded_;
    }

    void step(double) override {
        if (!race_) return;
        stuntline::Controls c;
        if (driver_ >= 0)
            c = stuntline::drive(kit_.drivers[size_t(driver_)], kit_, *race_, rng_);
        else
            c = human();
        race_->step(c);
    }

    void event(const SDL_Event& e) override {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad_) pad_ = SDL_OpenGamepad(e.gdevice.which);
        if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return;
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;
        if (e.key.key == SDLK_R && race_)
            race_->respawn(race_->layout().line[size_t(race_->sample())].piece);
        if (e.key.key == SDLK_TAB) {
            driver_ = driver_ < 0 ? kit_.driver("pro") : -1;
        }
        if (e.key.key == SDLK_N) select((track_ + 1) % int(kit_.tracks.size()));
    }

    void draw(base::Renderer& r, float) override {
        r.clear_color(0.55f, 0.72f, 0.92f);
        if (!race_) return;
        float pos[3], quat[4];
        race_->body(pos, quat);
        const float yaw = 2.0f * std::atan2(quat[1], quat[3]);  // close enough for a chase cam
        base::Camera cam;
        cam.eye[0] = pos[0] - std::sin(yaw) * kChaseDistance;
        cam.eye[1] = pos[1] + kChaseHeight;
        cam.eye[2] = pos[2] - std::cos(yaw) * kChaseDistance;
        cam.at[0] = pos[0];
        cam.at[1] = pos[1] + 1;
        cam.at[2] = pos[2];
        cam.fov_y_degrees = 65;
        cam.far_plane = 1500;
        r.camera(cam);

        // The canyon floor, far below.
        base::Box ground;
        ground.center[0] = pos[0];
        ground.center[1] = layout_.lowest - 30;
        ground.center[2] = pos[2];
        ground.size[0] = ground.size[2] = 2000;
        ground.size[1] = 1;
        ground.color[0] = 0.55f;
        ground.color[1] = 0.42f;
        ground.color[2] = 0.3f;
        r.box(ground);

        for (const stuntline::Segment& s : layout_.road) {
            base::Box b;
            for (int i = 0; i < 3; ++i) {
                b.center[i] = s.center[i];
                b.size[i] = 2 * s.half[i];
            }
            for (int i = 0; i < 4; ++i) b.quat[i] = s.quat[i];
            const float shade = s.piece % 2 ? 0.42f : 0.5f;
            b.color[0] = b.color[1] = shade;
            b.color[2] = shade + 0.04f;
            r.box(b);
        }
        // The line, at the finish.
        const stuntline::Sample& end = layout_.line.back();
        base::Box flag;
        flag.center[0] = end.pos[0];
        flag.center[1] = end.pos[1] + 3;
        flag.center[2] = end.pos[2];
        flag.size[0] = 12;
        flag.size[1] = 0.4f;
        flag.size[2] = 0.4f;
        flag.rotation[1] = end.yaw;
        r.box(flag);

        const stuntline::Car& car = kit_.car;
        base::Box body;
        // The body's position is its center of mass, half its height below the box's center.
        {
            const float x = quat[0], y = quat[1], z = quat[2], w = quat[3], h = car.half_height;
            body.center[0] = pos[0] + 2 * (x * y - w * z) * h;
            body.center[1] = pos[1] + (1 - 2 * (x * x + z * z)) * h;
            body.center[2] = pos[2] + 2 * (y * z + w * x) * h;
        }
        body.size[0] = 2 * car.half_width;
        body.size[1] = 2 * car.half_height;
        body.size[2] = 2 * car.half_length;
        for (int i = 0; i < 4; ++i) body.quat[i] = quat[i];
        body.color[0] = 0.95f;
        body.color[1] = 0.35f;
        body.color[2] = 0.15f;
        r.box(body);
        for (int w = 0; w < 4; ++w) {
            base::Box wheel;
            race_->wheel(w, wheel.center, wheel.quat);
            wheel.size[0] = std::max(0.15f, car.wheel_width);
            wheel.size[1] = wheel.size[2] = 2 * car.wheel_radius;
            wheel.sphere = true;
            wheel.color[0] = wheel.color[1] = wheel.color[2] = 0.12f;
            r.box(wheel);
        }
        const auto& line = layout_.line[size_t(race_->sample())];
        r.text(48, 2, "%s  %s  %5.1f s  %3.0f km/h  piece %d/%zu  crashes %d%s",
               track().name.c_str(),
               driver_ < 0 ? "you" : kit_.drivers[size_t(driver_)].name.c_str(),
               double(race_->seconds()), double(race_->speed() * 3.6f), line.piece + 1,
               track().lay.size(), race_->crashes, race_->finished() ? "   FINISHED" : "");
        if (!layout_.connected) r.text(48, 3, "track not valid: %s", layout_.problem.c_str());
    }

    void tools() override {
        ImGui::SetNextWindowPos(ImVec2(380, 470), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 230), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Stuntline")) {
            ImGui::End();
            return;
        }
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("track", track().name.c_str())) {
            for (size_t i = 0; i < kit_.tracks.size(); ++i)
                if (ImGui::Selectable(kit_.tracks[i].name.c_str(), int(i) == track_))
                    select(int(i));
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        const char* who = driver_ < 0 ? "you" : kit_.drivers[size_t(driver_)].name.c_str();
        if (ImGui::BeginCombo("driver", who)) {
            if (ImGui::Selectable("you", driver_ < 0)) driver_ = -1, restart();
            for (size_t i = 0; i < kit_.drivers.size(); ++i)
                if (ImGui::Selectable(kit_.drivers[i].name.c_str(), int(i) == driver_))
                    driver_ = int(i), restart();
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s", track().desc.c_str());
        if (ImGui::Button("Restart")) restart();
        ImGui::SameLine();
        if (ImGui::Button("Run every driver")) report_ = report();
        if (!report_.empty()) ImGui::TextUnformatted(report_.c_str());
        ImGui::End();
    }

private:
    const stuntline::Track& track() const { return kit_.tracks[size_t(track_)]; }

    void select(int t) {
        track_ = std::clamp(t, 0, int(kit_.tracks.size()) - 1);
        layout_ = stuntline::lay_out(kit_, track());
        results_.clear();
        report_.clear();
        restart();
    }

    void restart() {
        race_ = std::make_unique<stuntline::Race>(kit_, layout_);
        rng_ = base::Rng(seed_, 0x2545F4914F6CDD1Dull);
    }

    // A driver's headless race over the open track, once per driver and seed.
    moira::Expected<const stuntline::Result*> analysis(moira::Symbol name) {
        const int d = kit_.driver(std::string(name.str()));
        if (d < 0)
            return moira::Unexpected(base::not_found("no driver " + std::string(name.str())));
        auto key = std::make_tuple(track_, d, seed_);
        auto it = results_.find(key);
        if (it == results_.end())
            it = results_
                     .emplace(key, stuntline::race(kit_, layout_, kit_.drivers[size_t(d)], seed_,
                                                   track().par * kPatience))
                     .first;
        return &it->second;
    }

    std::string report() {
        std::string s = "driver   time     crashes (pieces)   airtime  top km/h  jump speeds\n";
        char line[256];
        for (const stuntline::Driver& d : kit_.drivers) {
            auto r = analysis(moira::Symbol(d.name));
            if (!r) continue;
            const stuntline::Result& x = **r;
            std::string pieces;
            for (int p : x.crash_pieces)
                pieces += (pieces.empty() ? "" : " ") + std::to_string(p + 1);
            std::string jumps;
            for (float v : x.gap_arrival) jumps += std::to_string(int(v * 3.6f)) + " ";
            std::snprintf(
                line, sizeof line, "%-8s %-8s %2d (%s)%*s %5.1f s  %6.0f    %s\n", d.name.c_str(),
                x.finished
                    ? (std::to_string(int(x.seconds * 10) / 10.0).substr(0, 5) + " s").c_str()
                    : "DNF",
                x.crashes, pieces.c_str(), int(std::max<size_t>(1, 12 - pieces.size())), "",
                double(x.airtime), double(x.top_speed * 3.6f), jumps.c_str());
            s += line;
        }
        return s;
    }

    stuntline::Controls human() const {
        stuntline::Controls c;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool typing = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
        if (keys && !typing) {
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) c.forward = 1;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) c.brake = 1;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) c.right -= 1;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) c.right += 1;
            if (keys[SDL_SCANCODE_SPACE]) c.handbrake = 1;
        }
        if (pad_) {
            const float throttle =
                float(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) / 32767.0f;
            const float brake =
                float(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) / 32767.0f;
            const float steer = float(SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f;
            c.forward = std::max(c.forward, throttle);
            c.brake = std::max(c.brake, brake);
            if (std::fabs(steer) > 0.12f) c.right = std::clamp(c.right + steer, -1.0f, 1.0f);
            if (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_SOUTH)) c.handbrake = 1;
        }
        // Reversing: brake held while (nearly) stopped drives backward.
        if (race_ && c.brake > 0 && race_->speed() < 1 && c.forward == 0) {
            c.forward = -c.brake;
            c.brake = 0;
        }
        return c;
    }

    stuntline::Kit kit_;
    bool loaded_ = false;
    int track_ = 0, driver_ = -1;
    stuntline::Layout layout_;
    std::unique_ptr<stuntline::Race> race_;
    std::map<std::tuple<int, int, uint64_t>, stuntline::Result> results_;
    std::string report_;
    uint64_t seed_ = 0;
    base::Rng rng_;
    SDL_Gamepad* pad_ = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    StuntlineGame game;
    return base::main(game, argc, argv, {.title = "Stuntline", .data_dir = STUNTLINE_DATA});
}
