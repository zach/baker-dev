// Hearth (game-stacks.md §4.3): does a household read as people? The households are
// household.hpp; this file is the game around them: the workbench (a timeline per villager,
// "why did you do that?", time controls, a spawner), the script words (hearth.), and drawing.
//
//   stacks_hearth                                     play
//   stacks_hearth sim --baker-seeds 1-3               soak 50 households for 30 days, per seed
//   stacks_hearth eval "{hearth.soak 10 7}" "{hearth.soak_report}"
//   stacks_hearth eval "{hearth.bench 150}"           microseconds per step for 150 villagers
//
// Keys: space pauses; 1 to 4 set the speed (1x, 60x, 600x, 3600x); tab picks the next
// villager; N spawns the next household.
#include "household.hpp"

#include <base/app.hpp>
#include <base/render.hpp>
#include <base/words.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static baker::Tweak<float> kSpeed{
    "hearth.speed", 60.0f, {.range = {1, 3600}, .desc = "Game seconds per real second"}};
static baker::Tweak<float> kWalkSpeed{
    "hearth.walk_speed", 1.4f, {.range = {0.2f, 5}, .desc = "Metres a second"}};
static baker::Tweak<int> kChoiceTop{
    "hearth.choice_top", 3, {.range = {1, 10}, .desc = "Picks among this many best"}};
static baker::Tweak<float> kChoiceWithin{
    "hearth.choice_within", 0.7f, {.range = {0, 1}, .desc = "...within this share of the best"}};
static baker::Tweak<float> kDistanceScale{
    "hearth.distance_scale", 25.0f, {.range = {1, 200}, .desc = "Metres at which appeal halves"}};
static baker::Tweak<float> kOffHours{
    "hearth.off_hours", 0.2f, {.range = {0, 1}, .desc = "Appeal outside a thing's hours"}};
static baker::Tweak<float> kIdleMinutes{
    "hearth.idle_minutes", 10.0f, {.range = {1, 120}, .desc = "Wait when nothing appeals"}};
static baker::Tweak<float> kStuckMinutes{
    "hearth.stuck_minutes", 60.0f, {.range = {5, 600}, .desc = "No progress this long is stuck"}};
static baker::Tweak<float> kStarveMinutes{
    "hearth.starve_minutes", 120.0f, {.range = {5, 1440}, .desc = "At zero this long starves"}};
static baker::Tweak<int> kOscillation{
    "hearth.oscillation_switches", 6, {.range = {2, 50}, .desc = "Back and forth this often"}};

namespace {

constexpr float kRadians = 0.017453292f;
constexpr int kTimeline = 144;  // samples: a day, every 10 game minutes

hearth::Rules current_rules() {
    hearth::Rules r;
    r.walk_speed = kWalkSpeed;
    r.choice_top = kChoiceTop;
    r.choice_within = kChoiceWithin;
    r.distance_scale = kDistanceScale;
    r.off_hours = kOffHours;
    r.idle_minutes = kIdleMinutes;
    r.stuck_minutes = kStuckMinutes;
    r.starve_minutes = kStarveMinutes;
    r.oscillation_switches = kOscillation;
    return r;
}

double per_day(int events, const hearth::Tally& t) {
    return t.character_days > 0 ? double(events) / t.character_days : 0;
}

class HearthGame final : public base::Game {
public:
    void bind(baker::Context& ctx) override {
        ctx_ = &ctx;
        ctx.add_type<hearth::Villager>();
        ctx.add_type<hearth::Thing>();
        clock_ = std::make_unique<baker::Scheduler>(ctx);
        (void)ctx.add(*clock_, baker::Symbol("clock"));
        auto doc = [](auto f, const char* text) {
            if (f) f->doc(text);
        };
        doc(ctx.func("hearth.spawn",
                     [this](baker::Symbol name) -> baker::Expected<void> {
                         const int h = kit_.house(std::string(name.str()));
                         if (h < 0)
                             return baker::Unexpected(
                                 base::not_found("no household " + std::string(name.str())));
                         spawn(h);
                         return {};
                     }),
            "Replaces the household with a new one of (households ...).");
        doc(ctx.func("hearth.speed", [this](double s) { speed_ = std::clamp(s, 0.0, 36000.0); }),
            "Game seconds per real second while playing; 0 pauses.");
        doc(ctx.func("hearth.day", [this] { return home_ ? home_->minute() / 1440 : 0.0; }),
            "Game days since the household moved in.");
        doc(ctx.func("hearth.why",
                     [this](baker::Symbol who) -> baker::Expected<std::string> {
                         hearth::Villager* v =
                             home_ ? home_->villager(std::string(who.str())) : nullptr;
                         if (!v)
                             return baker::Unexpected(
                                 base::not_found("no villager " + std::string(who.str())));
                         return home_->why(*v);
                     }),
            "A villager's last decision: the options, their scores, the pick, and its margin.");
        doc(ctx.func("hearth.need",
                     [this](baker::Symbol who, baker::Symbol need) -> baker::Expected<double> {
                         hearth::Villager* v =
                             home_ ? home_->villager(std::string(who.str())) : nullptr;
                         if (!v)
                             return baker::Unexpected(
                                 base::not_found("no villager " + std::string(who.str())));
                         return v->need(need);
                     }),
            "A villager's need, 0 to 100.");
        doc(ctx.func("hearth.events",
                     [this](baker::Symbol kind) -> baker::Expected<int64_t> {
                         if (!home_) return int64_t(0);
                         return count(home_->tally(), kind);
                     }),
            "In the household being played: stuck, oscillating, starving, script_errors, "
            "decisions, or interrupts so far.");
        doc(ctx.func("hearth.soak",
                     [this](int64_t households, double days) {
                         hearth::SoakOptions o;
                         o.households = int(households);
                         o.days = days;
                         o.seed = seed_;
                         o.threads = threads_;
                         kit_.rules = current_rules();
                         soak_ = hearth::soak(kit_, o);
                         return int64_t(soak_.tally.stuck + soak_.tally.oscillating +
                                        soak_.tally.starving);
                     }),
            "Runs households (the data's in turn) for days each, on worker threads, seeded "
            "from $seed; returns the failures. The measures below read it.");
        doc(ctx.func("hearth.soak_threads", [this](int64_t n) { threads_ = int(n); }),
            "Worker threads for the next soak; 0 is the machine's (at most 8).");
        doc(ctx.func("hearth.soak_events",
                     [this](baker::Symbol kind) -> baker::Expected<int64_t> {
                         return count(soak_.tally, kind);
                     }),
            "In the last soak: stuck, oscillating, starving, script_errors, decisions, or "
            "interrupts.");
        doc(ctx.func("hearth.soak_rate",
                     [this](baker::Symbol kind) -> baker::Expected<double> {
                         auto n = count(soak_.tally, kind);
                         if (!n) return baker::Unexpected(n.error());
                         return per_day(int(*n), soak_.tally);
                     }),
            "In the last soak: events of a kind per character-day.");
        doc(ctx.func("hearth.soak_used",
                     [this] {
                         return soak_.tally.things > 0
                                    ? double(soak_.tally.things_used) / double(soak_.tally.things)
                                    : 0.0;
                     }),
            "In the last soak: the share of things used at least once.");
        doc(ctx.func("hearth.soak_in_band",
                     [this](double low, double high) {
                         return hearth::share_in_band(soak_.tally, low, high);
                     }),
            "In the last soak: the share of villagers whose routine entropy (bits) is in band.");
        doc(ctx.func("hearth.soak_entropy",
                     [this] {
                         const auto& e = soak_.tally.entropy;
                         double s = 0;
                         for (double x : e) s += x;
                         return e.empty() ? 0.0 : s / double(e.size());
                     }),
            "In the last soak: the mean routine entropy, in bits.");
        doc(ctx.func("hearth.soak_kind_uses",
                     [this](baker::Symbol kind) -> baker::Expected<int64_t> {
                         const int k = kit_.kind(std::string(kind.str()));
                         if (k < 0 || size_t(k) >= soak_.tally.uses_by_kind.size())
                             return baker::Unexpected(
                                 base::not_found("no object " + std::string(kind.str())));
                         return int64_t(soak_.tally.uses_by_kind[size_t(k)]);
                     }),
            "In the last soak: how often villagers chose things of a kind.");
        doc(ctx.func("hearth.soak_report", [this] { return report(); }),
            "The last soak as a person reads it: the measures, uses by kind, and the first "
            "failures.");
        doc(ctx.func("hearth.bench",
                     [this](int64_t characters) {
                         kit_.rules = current_rules();
                         return hearth::bench(kit_, int(characters), 60);
                     }),
            "Microseconds one 60 Hz step of that many villagers takes, here, at 60x. Reads the "
            "wall clock: for a person, not an objective.");
    }

    void unbind(baker::Context&) override {
        home_.reset();
        clock_.reset();
        ctx_ = nullptr;
    }

    void load(const baker::Config& cfg) override {
        hearth::Kit next;
        std::string error;
        if (!hearth::load(cfg.root(), next, error)) {
            std::fprintf(stderr, "hearth: %s; keeping the last good data\n", error.c_str());
            return;
        }
        for (const baker::Node& n : cfg.autorun()) next.setup.push_back(n);
        const std::string open = loaded_ ? kit_.houses[size_t(house_)].name : std::string();
        home_.reset();  // it refers to the kit
        kit_ = std::move(next);
        kit_.rules = current_rules();
        loaded_ = true;
        const int h = kit_.house(open);
        spawn(h >= 0 ? h : 0);
    }

    void reset(uint64_t seed) override {
        seed_ = seed;
        if (loaded_) spawn(house_);
    }

    bool start(base::App&) override {
        if (!loaded_) std::fprintf(stderr, "hearth: the data has no usable households\n");
        speed_ = kSpeed;
        return loaded_;
    }

    void step(double dt) override {
        if (!home_ || speed_ <= 0) return;
        kit_.rules = current_rules();
        home_->step(dt * speed_);
        const int sample = int(home_->minute() / 10);
        if (sample != last_sample_) {
            last_sample_ = sample;
            for (size_t i = 0; i < home_->villagers().size() && i < timeline_.size(); ++i) {
                auto& line = timeline_[i];
                const auto& needs = home_->villagers()[i]->needs;
                for (size_t n = 0; n < needs.size() && n < line.size(); ++n) {
                    line[n][size_t(sample % kTimeline)] = needs[n];
                }
            }
        }
    }

    void event(const SDL_Event& e) override {
        const bool keys_taken = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
        if (e.type != SDL_EVENT_KEY_DOWN || keys_taken || e.key.repeat) return;
        switch (e.key.key) {
            case SDLK_SPACE: std::swap(speed_, paused_speed_); break;
            case SDLK_1: speed_ = 1; break;
            case SDLK_2: speed_ = 60; break;
            case SDLK_3: speed_ = 600; break;
            case SDLK_4: speed_ = 3600; break;
            case SDLK_TAB:
                if (home_ && !home_->villagers().empty())
                    selected_ = (selected_ + 1) % int(home_->villagers().size());
                break;
            case SDLK_N:
                if (loaded_) spawn((house_ + 1) % int(kit_.houses.size()));
                break;
            default: break;
        }
    }

    void draw(base::Renderer& r, float) override {
        r.clear_color(0.55f, 0.68f, 0.5f);
        if (!home_) return;
        const hearth::House& h = home_->house();
        base::Camera cam;
        const float span = std::max(h.size[0], h.size[1] * 1.6f);
        cam.eye[0] = 0;
        cam.eye[1] = span * 0.95f;
        cam.eye[2] = -span * 0.55f;
        cam.at[0] = 0;
        cam.at[1] = 0;
        cam.at[2] = 0.2f;
        cam.fov_y_degrees = 50;
        r.camera(cam);
        r.box(0, -0.1f, 0, h.size[0], 0.2f, h.size[1], 0.78f, 0.72f, 0.62f);
        for (const auto& w : h.walls) r.box(w[0], 0.6f, w[1], w[2], 1.2f, w[3], 0.9f, 0.88f, 0.84f);
        for (const auto& t : home_->things()) {
            const hearth::Kind& k = kit_.kinds[size_t(t->kind)];
            const float dim = t->users > 0 ? 1.0f : 0.8f;
            r.box(t->x, k.size[1] / 2, t->z, k.size[0], k.size[1], k.size[2], k.color[0] * dim,
                  k.color[1] * dim, k.color[2] * dim, t->yaw * kRadians);
        }
        for (size_t i = 0; i < home_->villagers().size(); ++i) {
            const hearth::Villager& v = *home_->villagers()[i];
            const bool lying =
                v.state == hearth::Villager::State::Using && std::string(v.action.str()) == "sleep";
            const float* c = v.resident.color;
            const float lift = int(i) == selected_ ? 0.15f : 0;
            if (lying)
                r.box(v.x, 0.7f, v.z, 0.45f, 0.3f, 1.6f, c[0], c[1], c[2]);
            else
                r.box(v.x, 0.85f + lift, v.z, 0.45f, 1.7f, 0.35f, c[0], c[1], c[2]);
        }
        const int m = int(home_->minute());
        r.text(2, 3, "%s   day %d  %02d:%02d   x%.0f", h.name.c_str(), m / 1440 + 1, (m / 60) % 24,
               m % 60, speed_);
    }

    void ui() override {
        if (!home_) return;
        ImGui::SetNextWindowPos(ImVec2(860, 40), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400, 620), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Hearth")) {
            ImGui::End();
            return;
        }
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("household", kit_.houses[size_t(house_)].name.c_str())) {
            for (size_t i = 0; i < kit_.houses.size(); ++i)
                if (ImGui::Selectable(kit_.houses[i].name.c_str(), int(i) == house_)) spawn(int(i));
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Respawn")) spawn(house_);
        float s = float(speed_);
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderFloat("speed", &s, 0, 3600, "x%.0f", ImGuiSliderFlags_Logarithmic))
            speed_ = s;
        const hearth::Tally t = home_->tally();
        ImGui::Text("stuck %d   oscillating %d   starving %d   errors %llu", t.stuck, t.oscillating,
                    t.starving, static_cast<unsigned long long>(t.script_errors));
        ImGui::Separator();
        auto& vs = home_->villagers();
        for (size_t i = 0; i < vs.size(); ++i) {
            const hearth::Villager& v = *vs[i];
            ImGui::PushID(int(i));
            if (ImGui::RadioButton(v.name.c_str(), selected_ == int(i))) selected_ = int(i);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", v.action.str().data());
            ImGui::PopID();
        }
        if (selected_ >= 0 && size_t(selected_) < vs.size()) {
            const hearth::Villager& v = *vs[size_t(selected_)];
            ImGui::Separator();
            for (size_t n = 0; n < kit_.needs.size(); ++n) {
                char label[48];
                std::snprintf(label, sizeof label, "%s %.0f", kit_.needs[n].name.c_str(),
                              double(v.needs[n]));
                ImGui::ProgressBar(v.needs[n] / 100, ImVec2(140, 0), label);
                ImGui::SameLine();
                if (size_t(selected_) < timeline_.size()) {
                    const auto& line = timeline_[size_t(selected_)][n];
                    ImGui::PushID(int(n));
                    ImGui::PlotLines("", line.data(), kTimeline, (last_sample_ + 1) % kTimeline,
                                     nullptr, 0, 100, ImVec2(200, 18));
                    ImGui::PopID();
                }
            }
            ImGui::Separator();
            ImGui::TextWrapped("why: %s", home_->why(v).c_str());
            ImGui::Separator();
            ImGui::BeginChild("log", ImVec2(0, 0));
            for (auto it = v.log.rbegin(); it != v.log.rend(); ++it)
                ImGui::TextUnformatted(it->c_str());
            ImGui::EndChild();
        }
        ImGui::End();
    }

private:
    static baker::Expected<int64_t> count(const hearth::Tally& t, baker::Symbol kind) {
        const std::string k(kind.str());
        if (k == "stuck") return int64_t(t.stuck);
        if (k == "oscillating") return int64_t(t.oscillating);
        if (k == "starving") return int64_t(t.starving);
        if (k == "script_errors") return int64_t(t.script_errors);
        if (k == "decisions") return int64_t(t.decisions);
        if (k == "interrupts") return int64_t(t.interrupts);
        return baker::Unexpected(base::not_found("no event kind " + k +
                                                 "; stuck, oscillating, starving, script_errors, "
                                                 "decisions, or interrupts"));
    }

    std::string report() const {
        const hearth::Tally& t = soak_.tally;
        char buf[512];
        std::snprintf(buf, sizeof buf,
                      "%d households, %d villagers, %.0f character-days on %d threads: stuck %d, "
                      "oscillating %d, starving %d, script errors %llu; %d of %d things used; "
                      "%llu decisions, %llu interrupts",
                      soak_.households, t.characters, t.character_days, soak_.threads, t.stuck,
                      t.oscillating, t.starving, static_cast<unsigned long long>(t.script_errors),
                      t.things_used, t.things, static_cast<unsigned long long>(t.decisions),
                      static_cast<unsigned long long>(t.interrupts));
        std::string s = buf;
        s += "; uses:";
        for (size_t k = 0; k < kit_.kinds.size() && k < t.uses_by_kind.size(); ++k)
            s += " " + kit_.kinds[k].name + " " + std::to_string(t.uses_by_kind[k]);
        for (const std::string& n : t.notes) s += "; " + n;
        for (const std::string& e : soak_.errors) s += "; setup: " + e;
        return s;
    }

    void spawn(int h) {
        home_.reset();
        house_ = std::clamp(h, 0, int(kit_.houses.size()) - 1);
        kit_.rules = current_rules();
        home_ = std::make_unique<hearth::Household>(kit_, house_, *ctx_, *clock_, seed_ + 1, "");
        timeline_.assign(home_->villagers().size(), {});
        for (auto& v : timeline_) v.assign(kit_.needs.size(), std::vector<float>(kTimeline, 0));
        last_sample_ = -1;
        selected_ = 0;
    }

    baker::Context* ctx_ = nullptr;
    std::unique_ptr<baker::Scheduler> clock_;
    hearth::Kit kit_;
    bool loaded_ = false;
    int house_ = 0;
    std::unique_ptr<hearth::Household> home_;
    hearth::SoakResult soak_;
    int threads_ = 0;
    uint64_t seed_ = 0;
    double speed_ = 60, paused_speed_ = 0;
    int selected_ = 0;
    int last_sample_ = -1;
    std::vector<std::vector<std::vector<float>>> timeline_;  // villager, need, sample
};

}  // namespace

int main(int argc, char** argv) {
    HearthGame game;
    return base::main(game, argc, argv, {.title = "Hearth", .data_dir = HEARTH_DATA});
}
