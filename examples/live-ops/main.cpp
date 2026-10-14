// Live ops: a shipped build takes overlays pushed while it runs, checked before they apply,
// and rolled back with one call.
//
//   baker_example_live_ops path/to/examples/live-ops
//
// The overlays are files here; in a game they arrive from a server as text, and are read
// with read_string just the same.
#include <baker/config.hpp>
#include <baker/shape.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <filesystem>
#include <string>

static baker::Tweak<float> kXp{"event.xp_multiplier", 1.0f};
static baker::Tweak<float> kDrops{"event.drop_rate", 0.05f};

namespace {

void status(const baker::Config& cfg) {
    auto banner = cfg.get<std::string_view>("event.banner", "");
    std::printf("  xp x%g, drops %g, banner \"%.*s\"\n", double(kXp), double(kDrops),
                int(banner ? banner->size() : 0), banner ? banner->data() : "");
}

// The test an overlay passes before it ships: the stack with it applied must satisfy the
// shapes. The same check runs in CI on every overlay file, before anyone pushes it.
bool passes(const baker::Config& cfg, const baker::Array& overlay) {
    baker::Ref<baker::Array> candidate = baker::merge(*cfg.root(), overlay);
    baker::shape::Set shapes;
    baker::shape::Report report;
    shapes.add(*candidate, report);
    baker::shape::check(*candidate, shapes, report);
    for (const baker::Error& e : report.errors)
        std::printf("  refused: %s\n", baker::to_string(e).data());
    return report.ok();
}

bool push(baker::Config& cfg, const char* name) {
    std::printf("push %s\n", name);
    auto overlay = baker::read_file(std::string("overlays/") + name + ".dta");
    if (!overlay) return std::printf("  %s\n", baker::to_string(overlay.error()).data()), false;
    if (!passes(cfg, **overlay)) return false;
    if (auto r = cfg.push_layer(name, *overlay); !r)
        return std::printf("  %s\n", baker::to_string(r.error()).data()), false;
    if (auto effect = cfg.layer_effect(name))
        for (const baker::LayerEffectEntry& e : *effect)
            std::printf("  %s: %s -> %s\n", e.path.c_str(),
                        e.below ? baker::to_string(*e.below).c_str() : "(none)",
                        baker::to_string(e.value).c_str());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::filesystem::current_path(argv[1]);  // so errors name files as the studio does

    // The environment boundary: a shipped build loads its data, then freezes. Only layers
    // can change after that, and only because live_layers says so: no reloads of the
    // shipped files and no live edits.
    baker::Config cfg({.live_layers = true});
    if (!cfg.push_layer("default", "data/default.dta")) return 1;
    cfg.freeze();
    std::printf("shipped:\n");
    status(cfg);

    if (!push(cfg, "2026-10-17-double-xp")) return 1;
    status(cfg);
    if (push(cfg, "2026-10-18-drops")) return 1;  // the typo never reaches players
    status(cfg);

    // Rollback: the layer goes, and the values below it return. Nothing is left behind.
    std::printf("roll back 2026-10-17-double-xp\n");
    if (!cfg.remove_layer("2026-10-17-double-xp")) return 1;
    status(cfg);
    std::printf("layers:");
    for (const std::string& name : baker::strings(cfg.layers())) std::printf(" %s", name.c_str());
    std::printf("\n");
    return 0;
}
