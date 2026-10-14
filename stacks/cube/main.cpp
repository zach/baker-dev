// The cube: the base's acceptance game (game-stacks.md §2.2, §6). Every value it uses is
// a tweak in data/default.dta; edit the file and save, and the next frame shows it, with
// the time from the save in the corner.
//
//   stacks_cube [--data DIR] [--renderer d3d11|d3d12|vulkan|gl|noop] [--headless] ...
//
// Script: {cube.reset} puts the cube back; {cube.angle} reads its angle in degrees;
// {cube.turns} counts the full turns since the last reset.
//
// The same binary is the agent's tool (stacks/agent.md): `stacks_cube sim` runs sim/*.dta
// headless and judges the runs by metrics/*.dta; `check`, `reference`, `diff`, `promote`.
#include <base/app.hpp>

#include <baker/std_interop.hpp>

#include <array>

static baker::Tweak<float> kSpin{
    "cube.spin", 45.0f, {.range = {-720, 720}, .desc = "Degrees per second"}};
static baker::Tweak<float> kSize{
    "cube.size", 1.0f, {.range = {0.1f, 5}, .desc = "Edge, in meters"}};
static baker::Tweak<std::array<float, 3>> kColor{"cube.color", {0.9f, 0.5f, 0.2f}};
static baker::Tweak<float> kTilt{
    "cube.tilt", 20.0f, {.range = {-90, 90}, .desc = "Degrees about x"}};
static baker::Tweak<float> kDistance{"camera.distance", 4.0f, {.range = {1, 50}}};
static baker::Tweak<float> kHeight{"camera.height", 1.5f, {.range = {-20, 20}}};
static baker::Tweak<std::array<float, 3>> kClear{"scene.clear", {0.10f, 0.10f, 0.12f}};

namespace {

constexpr float kRadiansPerDegree = 0.017453292f;

class CubeGame final : public base::Game {
public:
    void bind(baker::Context& ctx) override {
        (void)ctx.func("cube.reset", [this] { angle_ = previous_ = 0; });
        (void)ctx.func("cube.angle", [this] { return double(angle_); });
        (void)ctx.func("cube.turns", [this] { return double(angle_) / 360.0; });
    }
    void reset(uint64_t) override { angle_ = previous_ = 0; }
    void step(double dt) override {
        previous_ = angle_;
        angle_ += kSpin * float(dt);
    }
    void draw(base::Renderer& r, float alpha) override {
        const std::array<float, 3>& clear = kClear.get();
        r.clear_color(clear[0], clear[1], clear[2]);
        base::Camera camera;
        camera.eye[1] = kHeight;
        camera.eye[2] = -kDistance;
        r.camera(camera);

        const float angle = previous_ + (angle_ - previous_) * alpha;
        const std::array<float, 3>& c = kColor.get();
        base::Box box;
        box.size[0] = box.size[1] = box.size[2] = kSize;
        box.rotation[0] = kTilt * kRadiansPerDegree;
        box.rotation[1] = angle * kRadiansPerDegree;
        box.color[0] = c[0];
        box.color[1] = c[1];
        box.color[2] = c[2];
        r.box(box);
    }

private:
    float angle_ = 0, previous_ = 0;  // degrees
};

}  // namespace

int main(int argc, char** argv) {
    CubeGame game;
    return base::main(game, argc, argv, {.title = "Cube", .data_dir = CUBE_DATA});
}
