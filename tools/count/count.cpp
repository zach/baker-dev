// Small, fixed workloads for counting instructions (tools/count_instructions.py), one per
// case: `baker_count CASE N` runs the case N times after its setup. The tool runs each case
// with N and 2N under cachegrind, so setup and startup cancel and what remains is the
// instructions of N operations.
#include <baker/config.hpp>
#include <baker/parse.hpp>

#ifndef BAKER_COUNT_HOST
#define BAKER_COUNT_HOST 0
#endif
#if BAKER_COUNT_HOST
#include <baker/host/live.hpp>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

volatile double g_sink;

// Config-style text, as bench_parse makes it, about `bytes` long.
std::string config_text(size_t bytes) {
    std::string text;
    int id = 0;
    while (text.size() < bytes) {
        const std::string n = std::to_string(id++);
        text += "; entry " + n + "\n(entry_" + n + "\n   (name \"Entry number " + n +
                "\")\n   (enabled TRUE)\n   (position " + n +
                ".5 -12.25 0.0)\n   (color 0.2 0.4 0.8 1.0)\n   (tags fast loud blue)\n"
                "   (limits (min 0) (max 100) (step 0.5))\n"
                "   (on_hit {play_sfx \"hit.wav\"} {game add_score 10}))\n";
    }
    return text;
}

// An array of 20 children, (key0 0.5) ... (key19 19.5), as bench_lookup's.
baker::Ref<baker::Array> twenty() {
    std::string text = "(obj";
    for (int i = 0; i < 20; ++i)
        text += " (key" + std::to_string(i) + " " + std::to_string(i) + ".5)";
    text += ")";
    auto r = baker::read_string(text, "count.dta");
    if (!r) std::exit(2);
    return *r;
}

// bench_layers' case (D161, D164): a 2,000-item table, and an overlay changing 400 prices,
// each row pinned to the price below it when `pinned` (dev/syntax-layer-markers-design.md).
std::string table_text() {
    std::string s = "(shop\n";
    for (int i = 0; i < 2000; ++i)
        s += "   (item" + std::to_string(i) + " (price " + std::to_string(10 + i % 90) +
             ") (damage " + std::to_string(i % 7) + ") (name \"Item number " + std::to_string(i) +
             "\"))\n";
    return s + ")\n";
}
std::string overlay_text(bool pinned) {
    std::string s = "(shop\n";
    for (int i = 0; i < 2000; i += 5)
        s += "   (item" + std::to_string(i) + " (price " + std::to_string(20 + i % 90) +
             (pinned ? " #was " + std::to_string(10 + i % 90) : std::string()) + "))\n";
    return s + ")\n";
}

std::filesystem::path temp_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "baker_count";
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view which = argc > 1 ? argv[1] : "";
    const long n = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 0;
    if (which == "parse") {  // 16 KB of config text, read into a tree
        const std::string text = config_text(16 * 1024);
        for (long i = 0; i < n; ++i)
            if (!baker::read_string(text, "count.dta")) return 2;
    } else if (which == "lookup") {  // find_float by a held Symbol, the last of 20 children
        const auto root = twenty();
        const baker::Array& obj = *root->try_find("obj");
        const baker::Symbol key("key19");
        if (!obj.find_float(key)) return 2;  // a failed lookup would count its error instead
        for (long i = 0; i < n; ++i) g_sink = *obj.find_float(key);
    } else if (which == "lookup_text") {  // the same by text: interns each call
        const auto root = twenty();
        const baker::Array& obj = *root->try_find("obj");
        if (!obj.find_float("key19")) return 2;
        for (long i = 0; i < n; ++i) g_sink = *obj.find_float("key19");
    } else if (which == "set") {  // Config::set on its fast path: a scalar already in scratch
        const auto file = temp_dir() / "set.dta";
        write(file, "(speed 1)\n");
        baker::Config cfg({.tweaks = nullptr});
        if (!cfg.push_layer("default", file.string()) || !cfg.set("speed", baker::Node(2)))
            return 2;
        for (long i = 0; i < n; ++i)
            if (!cfg.set("speed", baker::Node(static_cast<int64_t>(i)))) return 2;
    } else if (which == "check" || which == "check_text" || which == "check_pinned") {
        // check_layer under bench_layers' policy: on a tree read once, on the text, or on
        // a tree whose every row is pinned
        baker::Config cfg({.edit_mode = false, .tweaks = nullptr});
        if (!cfg.push_layer_text("default", table_text())) return 2;
        const baker::Path shop[] = {baker::Path("shop")};
        const baker::LayerPolicy policy{.data_only = true,
                                        .scalars_only = true,
                                        .existing_only = true,
                                        .warnings_refuse = true,
                                        .allow = shop};
        const std::string text = overlay_text(which == "check_pinned");
        const auto tree = baker::read_string(text, "overlay");
        if (!tree) return 2;
        for (long i = 0; i < n; ++i) {
            auto r = which == "check_text" ? cfg.check_layer_text("overlay", text, policy)
                                           : cfg.check_layer("overlay", *tree, policy);
            if (!r || !r->ok()) return 2;
        }
#if BAKER_COUNT_HOST
    } else if (which == "frame") {  // a quiet frame of the host kit's Live, watching 2 files
        const auto dir = temp_dir() / "frame";
        std::filesystem::create_directories(dir / "data");
        write(dir / "baker-project.dta",
              "(project (layers (default \"data/default.dta\") (local \"data/local.dta\" "
              "optional)))\n");
        write(dir / "data" / "default.dta", "(speed 1)\n");
        baker::Context ctx;
        baker::host::Live live(ctx);
        if (!live.open({.project = (dir / "data").string()})) return 2;
        for (long i = 0; i < n; ++i) {
            live.begin_frame();
            live.end_frame();
        }
#endif
    } else {
        std::fprintf(
            stderr,
            "usage: baker_count CASE N; cases: parse lookup lookup_text set check check_text "
            "check_pinned%s\n",
            BAKER_COUNT_HOST ? " frame" : "");
        return 2;
    }
    return 0;
}
