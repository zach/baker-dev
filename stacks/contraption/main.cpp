// Contraption (game-stacks.md §4.1): physics puzzles, side on. The puzzles are puzzle.hpp;
// this file is the game around them: placing pieces with the mouse, running the machine,
// the script words (puzzle.), the workbench, and drawing.
//
//   stacks_contraption                          play
//   stacks_contraption sim --baker-seeds 1-5    every level: intended, robust, uncheesed
//   stacks_contraption eval "{puzzle.load first_drop}" "{puzzle.fewest_pieces 200}"
//
// Mouse: left places the tray piece you picked, right takes back the nearest; the wheel (or
// Q and E) turns it. Space runs and stops; R resets; N and P change level.
#include "puzzle.hpp"

#include <base/app.hpp>
#include <base/render.hpp>
#include <base/words.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static baker::Tweak<float> kRunSeconds{
    "contraption.run_seconds", 12.0f, {.range = {1, 120}, .desc = "An attempt's time limit"}};
static baker::Tweak<float> kHoldSeconds{
    "contraption.hold_seconds", 0.5f, {.range = {0, 10}, .desc = "How long the goal must hold"}};
static baker::Tweak<float> kGrid{
    "contraption.grid", 0.05f, {.range = {0, 1}, .desc = "Metres a placement snaps to"}};
static baker::Tweak<float> kAngleStep{
    "contraption.angle_step", 5.0f, {.range = {0, 45}, .desc = "Degrees a placement snaps to"}};

namespace {

using contraption::Piece;

constexpr float kFovDegrees = 50;
constexpr float kRadians = 0.017453292f;

contraption::Rules current_rules() {
    contraption::Rules r;
    r.run_seconds = kRunSeconds;
    r.hold_seconds = kHoldSeconds;
    r.grid = kGrid;
    r.angle_step = kAngleStep;
    return r;
}

class ContraptionGame final : public base::Game {
public:
    void bind(baker::Context& ctx) override {
        ctx_ = &ctx;
        auto doc = [](auto f, const char* text) {
            if (f) f->doc(text);
        };
        doc(ctx.func("puzzle.load",
                     [this](baker::Symbol name) -> baker::Expected<void> {
                         const int l = catalog_.level(std::string(name.str()));
                         if (l < 0)
                             return baker::Unexpected(
                                 base::not_found("no level " + std::string(name.str())));
                         select(l);
                         return {};
                     }),
            "Opens a level of (levels ...), with the tray empty.");
        doc(ctx.func("puzzle.level", [this] { return baker::Symbol(level().name); }),
            "The open level.");
        doc(ctx.func("puzzle.place",
                     [this](baker::Symbol kind, double x, double y,
                            double angle) -> baker::Expected<void> {
                         const int k = catalog_.kind(std::string(kind.str()));
                         if (k < 0)
                             return baker::Unexpected(
                                 base::not_found("no kind " + std::string(kind.str())));
                         Piece p;
                         p.name = "placed" + std::to_string(placed_.size());
                         p.kind = k;
                         p.x = float(x);
                         p.y = float(y);
                         p.angle = float(angle);
                         placed_.push_back(p);
                         stop();
                         return {};
                     }),
            "Places a piece, as the player would (not snapped): kind, x, y, degrees.");
        doc(ctx.func("puzzle.place_intended",
                     [this] {
                         placed_ = level().intended;
                         stop();
                     }),
            "Places the level's intended solution.");
        doc(ctx.func("puzzle.clear",
                     [this] {
                         placed_.clear();
                         stop();
                     }),
            "Takes back every placed piece.");
        doc(ctx.func("puzzle.run", [this] { start_run(); }),
            "Starts the machine; the game's fixed steps run it.");
        doc(ctx.func("puzzle.solved", [this] { return solved_; }),
            "Whether the running machine has solved the level.");
        doc(ctx.func("puzzle.in",
                     [this](baker::Symbol a, baker::Symbol b) -> baker::Expected<bool> {
                         if (!judging_) return false;
                         const int ia = judging_->find(std::string(a.str()));
                         const int ib = judging_->find(std::string(b.str()));
                         if (ia < 0 || ib < 0)
                             return baker::Unexpected(base::not_found(
                                 "puzzle.in names a piece the level does not have: " +
                                 std::string(ia < 0 ? a.str() : b.str())));
                         return judging_->inside(ia, ib);
                     }),
            "In a goal: whether piece a's center is inside bucket b.");
        doc(ctx.func("puzzle.intended_pieces", [this] { return int64_t(level().intended.size()); }),
            "How many tray pieces the intended solution uses.");
        doc(ctx.func("puzzle.solves_intended",
                     [this] {
                         return contraption::attempt(catalog_, level(), level().intended,
                                                     current_rules(), goal())
                             .solved;
                     }),
            "Whether the intended solution, placed exactly, solves the level.");
        doc(ctx.func("puzzle.intended_seconds",
                     [this] {
                         const auto a = contraption::attempt(catalog_, level(), level().intended,
                                                             current_rules(), goal());
                         return a.solved ? double(a.seconds) : -1.0;
                     }),
            "When the intended solution solves the level, in seconds; -1 if it does not.");
        doc(ctx.func("puzzle.robustness",
                     [this](int64_t samples, double metres, double degrees) {
                         return contraption::robustness(catalog_, level(), current_rules(), goal(),
                                                        int(samples), float(metres), float(degrees),
                                                        seed_);
                     }),
            "The share of perturbed intended solutions that solve: samples, metres, degrees.");
        doc(ctx.func("puzzle.fewest_pieces",
                     [this](int64_t tries) {
                         cheese_ = contraption::cheese(catalog_, level(), current_rules(), goal(),
                                                       int(tries), seed_);
                         return int64_t(cheese_.fewest >= 0 ? cheese_.fewest
                                                            : int(level().intended.size()));
                     }),
            "Searches for a solution with fewer pieces than intended (tries per count); "
            "returns the fewest found, or the intended count if it found none.");
        doc(ctx.func("puzzle.cheese_route",
                     [this] {
                         return cheese_.fewest < 0 ? std::string("none found")
                                                   : contraption::to_dta(cheese_.found, catalog_);
                     }),
            "The cheaper solution the last search found, as data a scenario can place.");
    }

    void load(const baker::Config& cfg) override {
        contraption::Catalog next;
        std::string error;
        if (!contraption::load(cfg.root(), next, error)) {
            std::fprintf(stderr, "contraption: %s; keeping the last good data\n", error.c_str());
            return;
        }
        const std::string open = loaded_ ? level().name : std::string();
        catalog_ = std::move(next);
        loaded_ = true;
        const int l = catalog_.level(open);
        level_ = l >= 0 ? l : 0;
        // Placed pieces keep their places; a kind that is gone takes its pieces with it.
        placed_.erase(
            std::remove_if(placed_.begin(), placed_.end(),
                           [&](const Piece& p) { return p.kind >= int(catalog_.kinds.size()); }),
            placed_.end());
        stop();
    }

    void reset(uint64_t seed) override {
        seed_ = seed;
        if (loaded_) select(level_);
    }

    bool start(base::App&) override {
        if (!loaded_) std::fprintf(stderr, "contraption: the data has no usable levels\n");
        return loaded_;
    }

    void step(double) override {
        if (!run_ || done_) return;
        run_->step(1.0f / 60.0f);
        ++steps_;
        const contraption::Rules r = current_rules();
        held_ = goal()(*run_) ? held_ + 1 : 0;
        if (held_ >= std::max(1, int(r.hold_seconds * 60))) {
            solved_ = true;
            done_ = true;
        } else if (steps_ >= int(r.run_seconds * 60)) {
            done_ = true;
        }
    }

    void event(const SDL_Event& e) override {
        const bool mouse_taken = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
        const bool keys_taken = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            cursor(e.motion.windowID, e.motion.x, e.motion.y);
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouse_taken && !run_) {
            cursor(e.button.windowID, e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) place_at_cursor();
            if (e.button.button == SDL_BUTTON_RIGHT) remove_nearest();
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL && !mouse_taken) {
            angle_ += (e.wheel.y > 0 ? 1.0f : -1.0f) * std::max(1.0f, float(kAngleStep));
        } else if (e.type == SDL_EVENT_KEY_DOWN && !keys_taken && !e.key.repeat) {
            switch (e.key.key) {
                case SDLK_SPACE: run_ ? stop() : start_run(); break;
                case SDLK_R: stop(); break;
                case SDLK_Q: angle_ += std::max(1.0f, float(kAngleStep)); break;
                case SDLK_E: angle_ -= std::max(1.0f, float(kAngleStep)); break;
                case SDLK_N: select((level_ + 1) % int(catalog_.levels.size())); break;
                case SDLK_P:
                    select((level_ + int(catalog_.levels.size()) - 1) %
                           int(catalog_.levels.size()));
                    break;
                default: break;
            }
        }
    }

    void draw(base::Renderer& r, float) override {
        r.clear_color(0.62f, 0.74f, 0.86f);
        if (!loaded_) return;
        const contraption::Level& l = level();
        base::Camera cam;
        camera_center(cam_x_, cam_y_, cam_d_);
        cam.eye[0] = cam_x_;
        cam.eye[1] = cam_y_;
        cam.eye[2] = -cam_d_;
        cam.at[0] = cam_x_;
        cam.at[1] = cam_y_;
        cam.fov_y_degrees = kFovDegrees;
        r.camera(cam);
        box(r, 0, -0.5f, 0, 0, 40, 1, 2, 0.45f, 0.4f, 0.32f);

        if (show_intended_ && !run_)
            for (const Piece& p : l.intended)
                draw_piece(r, p, p.x, p.y, p.angle * kRadians, nullptr, 0.6f, true);
        if (run_) {
            for (size_t i = 0; i < run_->count(); ++i) {
                const auto pose = run_->pose(i);
                draw_piece(r, run_->piece(i), pose.x, pose.y, pose.angle_rad, pose.quat, 0, false);
            }
        } else {
            for (const std::vector<Piece>* list :
                 {&l.pieces, static_cast<const std::vector<Piece>*>(&placed_)})
                for (const Piece& p : *list)
                    draw_piece(r, p, p.x, p.y, p.angle * kRadians, nullptr, 0, false);
            // The piece under the mouse, where a click would put it.
            if (selected_ >= 0 && remaining(selected_) > 0) {
                Piece ghost = contraption::snapped(pending(), current_rules());
                draw_piece(r, ghost, ghost.x, ghost.y, ghost.angle * kRadians, nullptr, -0.3f,
                           true);
            }
        }
        r.text(48, 2, "%s%s", l.name.c_str(),
               solved_ ? "   SOLVED"
               : run_  ? (done_ ? "   not solved: R to reset" : "   running")
                       : "");
    }

    void ui() override {
        if (!loaded_) return;
        ImGui::SetNextWindowPos(ImVec2(380, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 150), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Contraption")) {
            ImGui::End();
            return;
        }
        const contraption::Level& l = level();
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo("level", l.name.c_str())) {
            for (size_t i = 0; i < catalog_.levels.size(); ++i)
                if (ImGui::Selectable(catalog_.levels[i].name.c_str(), int(i) == level_))
                    select(int(i));
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Checkbox("intended", &show_intended_);
        ImGui::TextWrapped("%s", l.desc.c_str());
        for (const auto& [kind, count] : l.tray) {
            char label[64];
            std::snprintf(label, sizeof label, "%s x%d", catalog_.kinds[size_t(kind)].name.c_str(),
                          remaining(kind));
            if (ImGui::RadioButton(label, selected_ == kind)) selected_ = kind;
            ImGui::SameLine();
        }
        ImGui::Text("angle %.0f", double(angle_));
        if (ImGui::Button(run_ ? "Stop (space)" : "Run (space)")) run_ ? stop() : start_run();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) placed_.clear(), stop();
        ImGui::SameLine();
        if (ImGui::Button("Place intended")) placed_ = l.intended, stop();
        ImGui::SameLine();
        if (ImGui::Button("Robustness")) {
            robust_ =
                contraption::robustness(catalog_, l, current_rules(), goal(), 50, 0.005f, 2, seed_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Search cheese"))
            cheese_ = contraption::cheese(catalog_, l, current_rules(), goal(), 150, seed_);
        if (robust_ >= 0) ImGui::Text("intended solves %.0f%% at +-5 mm, +-2 deg", robust_ * 100);
        if (cheese_.tried > 0)
            ImGui::TextWrapped("%s (%d tries)",
                               cheese_.fewest >= 0
                                   ? ("cheese with " + std::to_string(cheese_.fewest) +
                                      " pieces: " + contraption::to_dta(cheese_.found, catalog_))
                                         .c_str()
                                   : "no cheaper solution found",
                               cheese_.tried);
        ImGui::End();
    }

private:
    static void box(base::Renderer& r, float x, float y, float z, float angle, float w, float h,
                    float d, float cr, float cg, float cb, const float* quat = nullptr,
                    bool sphere = false) {
        base::Box b;
        b.center[0] = x;
        b.center[1] = y;
        b.center[2] = z;
        b.size[0] = w;
        b.size[1] = h;
        b.size[2] = d;
        b.rotation[2] = angle;
        if (quat)
            for (int i = 0; i < 4; ++i) b.quat[i] = quat[i];
        b.color[0] = cr;
        b.color[1] = cg;
        b.color[2] = cb;
        b.sphere = sphere;
        r.box(b);
    }

    void draw_piece(base::Renderer& r, const Piece& p, float x, float y, float angle,
                    const float* quat, float z, bool ghost) {
        const contraption::Kind& k = catalog_.kinds[size_t(p.kind)];
        const float hue = float(p.kind) / float(std::max<size_t>(1, catalog_.kinds.size()));
        float c[3] = {0.55f + 0.4f * std::sin(6.28f * hue),
                      0.55f + 0.4f * std::sin(6.28f * (hue + 0.33f)),
                      0.55f + 0.4f * std::sin(6.28f * (hue + 0.66f))};
        if (ghost)
            for (float& v : c) v = v * 0.4f + 0.3f;
        if (k.shape == contraption::Shape::Sphere) {
            box(r, x, y, z, angle, k.w, k.w, k.w, c[0], c[1], c[2], quat, true);
        } else if (k.shape == contraption::Shape::Bucket) {
            const float t = 0.1f, cs = std::cos(angle), sn = std::sin(angle);
            auto part = [&](float lx, float ly, float w, float h) {
                box(r, x + cs * lx - sn * ly, y + sn * lx + cs * ly, z, angle, w, h, 1.0f, c[0],
                    c[1], c[2]);
            };
            part(0, -k.h / 2 + t / 2, k.w, t);
            part(-k.w / 2 + t / 2, 0, t, k.h);
            part(k.w / 2 - t / 2, 0, t, k.h);
        } else {
            box(r, x, y, z, angle, k.w, k.h, 1.0f, c[0], c[1], c[2], quat);
        }
    }

    const contraption::Level& level() const { return catalog_.levels[size_t(level_)]; }

    contraption::GoalTest goal() {
        return [this](contraption::Run& run) {
            const baker::Node& g = level().goal;
            if (!ctx_ || g.type() != baker::Type::Command) return false;
            judging_ = &run;
            auto r = ctx_->eval(g);
            judging_ = nullptr;
            return r && base::truthy(*r);
        };
    }

    void select(int l) {
        level_ = std::clamp(l, 0, int(catalog_.levels.size()) - 1);
        placed_.clear();
        selected_ = level().tray.empty() ? -1 : level().tray[0].first;
        angle_ = 0;
        robust_ = -1;
        cheese_ = {};
        stop();
    }

    void start_run() {
        run_ = std::make_unique<contraption::Run>(catalog_, level(), placed_);
        steps_ = held_ = 0;
        solved_ = done_ = false;
    }
    void stop() {
        run_.reset();
        solved_ = done_ = false;
    }

    int remaining(int kind) const {
        int n = 0;
        for (const auto& [k, count] : level().tray)
            if (k == kind) n += count;
        for (const Piece& p : placed_)
            if (p.kind == kind) --n;
        return n;
    }

    Piece pending() const {
        Piece p;
        p.name = "placed" + std::to_string(placed_.size());
        p.kind = selected_;
        p.x = mouse_x_;
        p.y = mouse_y_;
        p.angle = angle_;
        return p;
    }

    void place_at_cursor() {
        if (selected_ < 0 || remaining(selected_) <= 0) return;
        placed_.push_back(contraption::snapped(pending(), current_rules()));
    }

    void remove_nearest() {
        auto nearest = placed_.end();
        float best = 1.0f;  // within a metre
        for (auto it = placed_.begin(); it != placed_.end(); ++it) {
            const float d = std::hypot(it->x - mouse_x_, it->y - mouse_y_);
            if (d < best) best = d, nearest = it;
        }
        if (nearest != placed_.end()) placed_.erase(nearest);
    }

    // The camera frames the level's bounds, side on.
    void camera_center(float& x, float& y, float& d) const {
        const contraption::Level& l = level();
        x = (l.bounds[0] + l.bounds[2]) / 2;
        y = (l.bounds[1] + l.bounds[3]) / 2;
        const float half = std::max((l.bounds[2] - l.bounds[0]) / 2 / (16.0f / 9.0f),
                                    (l.bounds[3] - l.bounds[1]) / 2) +
                           1.0f;
        d = half / std::tan(kFovDegrees * kRadians / 2);
    }

    // The mouse, on the plane z = 0.
    void cursor(SDL_WindowID id, float mx, float my) {
        SDL_Window* w = SDL_GetWindowFromID(id);
        int width = 0, height = 0;
        if (!w || !SDL_GetWindowSize(w, &width, &height) || width == 0 || height == 0) return;
        const float t = std::tan(kFovDegrees * kRadians / 2) * cam_d_;
        const float aspect = float(width) / float(height);
        mouse_x_ = cam_x_ + (2 * mx / float(width) - 1) * t * aspect;
        mouse_y_ = cam_y_ + (1 - 2 * my / float(height)) * t;
    }

    baker::Context* ctx_ = nullptr;
    contraption::Catalog catalog_;
    bool loaded_ = false;
    int level_ = 0;
    std::vector<Piece> placed_;
    std::unique_ptr<contraption::Run> run_;
    contraption::Run* judging_ = nullptr;
    int steps_ = 0, held_ = 0;
    bool solved_ = false, done_ = false, show_intended_ = false;
    int selected_ = -1;
    float angle_ = 0, mouse_x_ = 0, mouse_y_ = 0;
    float cam_x_ = 0, cam_y_ = 4, cam_d_ = 12;
    double robust_ = -1;
    contraption::Cheese cheese_;
    uint64_t seed_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    ContraptionGame game;
    return base::main(game, argc, argv, {.title = "Contraption", .data_dir = CONTRAPTION_DATA});
}
