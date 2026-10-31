// Small, fixed workloads for counting instructions (tools/count_instructions.py), one per
// case: `baker_count CASE N` runs the case N times after its setup. The tool runs each case
// with N and 2N under cachegrind, so setup and startup cancel and what remains is the
// instructions of N operations.
#include <baker/config.hpp>
#include <baker/object.hpp>
#include <baker/parse.hpp>
#include <baker/script.hpp>
#include <baker/std_interop.hpp>

#ifndef BAKER_COUNT_DECLARED  // CMake sets it where Context::declare takes a body (D213)
#define BAKER_COUNT_DECLARED 0
#endif
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

struct HitMsg {
    static constexpr auto name = "hit";
    int points;
};

// A type with a message handler two levels up its chain: a send walks to it.
struct Unit : baker::Object<Unit> {
    int hp = 1'000'000;
    int on_hit(const HitMsg& m) { return hp -= m.points; }
    static void bind(baker::Binder<Unit>& b) { b.on<HitMsg>(&Unit::on_hit); }
};
struct Soldier : baker::Object<Soldier, Unit> {
    static void bind(baker::Binder<Soldier>&) {}
};
struct Captain : baker::Object<Captain, Soldier> {
    static void bind(baker::Binder<Captain>&) {}
};

// Each script case's loop is a function of its own, never inlined, so what the compiler does
// with main() as cases are added cannot move another case's count.

// `text` evaluated n times, as a game evaluating its data does: one parse, then evals.
[[gnu::noinline]] int eval_n(baker::Context& ctx, const char* text, long n) {
    auto tree = baker::read_string(text, "count.dta");
    if (!tree) return 2;
    const baker::Node& node = (**tree)[0];
    if (!ctx.eval(node)) return 2;  // a failed call would count its error instead
    for (long i = 0; i < n; ++i) {
        auto r = ctx.eval(node);
        g_sink = r ? static_cast<double>(r->try_float().value_or(0)) : -1.0;
    }
    return 0;
}

// HitMsg sent to `to` n times.
[[gnu::noinline]] int send_n(Captain* to, long n) {
    if (!to->handle(HitMsg{1})) return 2;
    for (long i = 0; i < n; ++i) {
        auto r = to->handle(HitMsg{static_cast<int>(i & 1)});
        g_sink = r ? static_cast<double>(r->try_int().value_or(0)) : -1.0;
    }
    return 0;
}

// HitMsg sent to all of `all`, n times.
[[gnu::noinline]] int send_all_n(std::span<baker::ObjectBase* const> all, long n) {
    for (long i = 0; i < n; ++i) {
        auto r = baker::handle_all(all, HitMsg{static_cast<int>(i & 1)});
        g_sink = r ? static_cast<double>(*r) : -1.0;
    }
    return 0;
}

std::filesystem::path temp_dir() {
    const auto dir = std::filesystem::temp_directory_path() / "baker_count";
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, std::string_view text) {
    std::ofstream(file, std::ios::binary) << text;
}

// bench_layers' shape of data: a 2,000-item table, and an overlay of 400 of its prices,
// each row `(price v` + `pin` + `)`.
std::string table_text() {
    std::string s = "(shop\n";
    for (int i = 0; i < 2000; ++i)
        s += "   (item" + std::to_string(i) + " (price " + std::to_string(10 + i % 90) +
             ") (damage " + std::to_string(i % 7) + ") (tags a b c))\n";
    return s + ")\n";
}
std::string overlay_text(bool pinned) {
    std::string s = "(shop\n";
    for (int i = 0; i < 2000; i += 5)
        s += "   (item" + std::to_string(i) + " (price " + std::to_string(20 + i % 90) +
             (pinned ? " (@was " + std::to_string(10 + i % 90) + ")" : std::string()) + "))\n";
    return s + ")\n";
}
// The same 400 items, each extending its list: (tags #append d).
std::string append_text() {
    std::string s = "(shop\n";
    for (int i = 0; i < 2000; i += 5) s += "   (item" + std::to_string(i) + " (tags #append d))\n";
    return s + ")\n";
}

// Config::check_layer of `over` on the table, n times.
int check_n(std::string_view over, long n) {
    baker::Config cfg({.edit_mode = false, .tweaks = nullptr});
    if (!cfg.push_layer_text("default", table_text())) return 2;
    auto tree = baker::read_string(over, "over.dta");
    if (!tree) {
        std::fprintf(stderr, "usage: this tree cannot read the case's text\n");
        return 2;
    }
    for (long i = 0; i < n; ++i) {
        auto r = cfg.check_layer("over", *tree);
        if (!r) return 2;
        g_sink = static_cast<double>(r->effect.size());
    }
    return 0;
}

// baker::merge of `over` on the table, n times.
int merge_n(std::string_view over, long n) {
    auto base = baker::read_string(table_text(), "table.dta");
    auto tree = baker::read_string(over, "over.dta");
    if (!base || !tree) {
        std::fprintf(stderr, "usage: this tree cannot read the case's text\n");
        return 2;
    }
    for (long i = 0; i < n; ++i) g_sink = static_cast<double>(baker::merge(**base, **tree)->size());
    return 0;
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
    } else if (which == "merge") {  // 400 prices merged over a 2,000-item table
        return merge_n(overlay_text(false), n);
    } else if (which == "merge_pinned") {  // the same, each row pinned (@was v)
        return merge_n(overlay_text(true), n);
    } else if (which == "merge_append") {  // 400 lists of the table, each (tags #append d)
        return merge_n(append_text(), n);
    } else if (which == "check") {  // Config::check_layer of the 400 prices on the table
        return check_n(overlay_text(false), n);
    } else if (which == "check_pinned") {  // the same, each row pinned (@was v)
        return check_n(overlay_text(true), n);
    } else if (which == "call") {  // {add 1 2}: a native function of two ints
        baker::Context ctx;
        if (!ctx.func("add", [](int64_t a, int64_t b) { return a + b; })) return 2;
        return eval_n(ctx, "{add 1 2}", n);
    } else if (which == "var") {  // {add $x 1}: an argument evaluated before the read
        baker::Context ctx;
        if (!ctx.func("add", [](int64_t a, int64_t b) { return a + b; })) return 2;
        if (!ctx.eval("{set $x 2}")) return 2;
        return eval_n(ctx, "{add $x 1}", n);
    } else if (which == "overload") {  // {s2 1.5 2}: an overload set of two, the second taken
        // {+} was this case until D262 made it one variadic native (D267).
        baker::Context ctx;
        if (!ctx.func("s2", [](int64_t a, int64_t b) { return a + b; })) return 2;
        if (!ctx.func("s2", [](double a, int64_t b) { return a + static_cast<double>(b); }))
            return 2;
        return eval_n(ctx, "{s2 1.5 2}", n);
#if BAKER_COUNT_DECLARED
    } else if (which == "declared") {  // {add 1 2} declared from a description, with a body
        baker::Context ctx;
        static constexpr std::string_view kInt[] = {"int"};
        const baker::ParamDescription params[] = {{"int", "value", false, kInt},
                                                  {"int", "value", false, kInt}};
        const baker::OverloadDescription overloads[] = {{params, "int"}};
        if (!ctx.declare("add", overloads, [](baker::Args& a) -> baker::Expected<baker::Node> {
                return baker::Node(a.raw(0).try_int().value_or(0) + a.raw(1).try_int().value_or(0));
            }))
            return 2;
        return eval_n(ctx, "{add 1 2}", n);
#endif
    } else if (which == "message") {  // a typed message from C++, handled two types up
        baker::Context ctx;
        Captain captain;
        if (!ctx.add(captain, "captain")) return 2;
        return send_n(&captain, n);
    } else if (which == "message_loose") {  // the same to an object in no Context
        Captain loose;
        return send_n(&loose, n);
    } else if (which == "handle_all") {  // one call sending to 64 objects
        std::vector<Captain> crowd(64);
        std::vector<baker::ObjectBase*> ptrs;
        for (Captain& c : crowd) ptrs.push_back(&c);
        return send_all_n(ptrs, n);
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
        std::fprintf(stderr,
                     "usage: baker_count CASE N; cases: parse lookup lookup_text set merge "
                     "merge_pinned merge_append check check_pinned call overload%s message "
                     "message_loose handle_all%s\n",
                     BAKER_COUNT_DECLARED ? " declared" : "", BAKER_COUNT_HOST ? " frame" : "");
        return 2;
    }
    return 0;
}
