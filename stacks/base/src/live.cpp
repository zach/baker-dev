#include "base/live.hpp"

#include <baker/project.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <filesystem>
#include <utility>

namespace base {

DataStack data_stack(const std::string& data_dir) {
    DataStack stack;
    // The stack's baker-project.dta, beside its data directory, is what the game and its
    // tools (an editor, a checker) all read (D172); the kit finds it from the data
    // directory up. A packaged game has none beside its data, and gets the same stack
    // from `built`.
    std::error_code ec;
    if (std::filesystem::exists(
            std::filesystem::path(data_dir).parent_path() / baker::kProjectFileName, ec))
        stack.project = data_dir;
    const baker::ProjectEntry levels[] = {
        {.value = "engine"}, {.value = "game"}, {.value = "task"}};
    const baker::ProjectEntry dirs[] = {{.value = baker::Symbol(data_dir)}};
    auto in_data = [&](const char* f) { return baker::Symbol(data_dir + "/" + f); };
    const baker::ProjectLayer layers[] = {
        {.name = "default", .path = in_data("default.dta")},
        {.name = "local", .path = in_data("local.dta"), .optional = true, .dev_only = true},
        {.name = "agent", .path = in_data("agent.dta"), .optional = true, .dev_only = true}};
    stack.built.pack_levels = baker::List<baker::ProjectEntry>(levels);
    stack.built.include_dirs = baker::List<baker::ProjectEntry>(dirs);
    stack.built.layers = baker::List<baker::ProjectLayer>(layers);
    // sim/ and metrics/ sit beside data/ (game-stacks.md §2.4).
    auto beside = [&](const char* dir) {
        return baker::Symbol(
            (std::filesystem::path(data_dir).parent_path() / dir).generic_string());
    };
    const baker::ProjectEntry scenarios[] = {{.value = beside("sim")}};
    const baker::ProjectEntry objectives[] = {{.value = beside("metrics")}};
    stack.built.scenarios = baker::List<baker::ProjectEntry>(scenarios);
    stack.built.objectives = baker::List<baker::ProjectEntry>(objectives);
    return stack;
}

bool Live::open(const LiveOptions& options) {
    data_dir_ = options.data_dir;
    changed_ = options.changed;
    const DataStack stack = data_stack(data_dir_);

    bind_live_loop(ctx_, [this] { return loop_report(config()); });
    baker::host::LiveOptions o;
    o.project = stack.project;
    o.built = stack.project.empty() ? &stack.built : nullptr;
    if (!options.local) o.open.skip.add("local");
    o.watch = options.watch;
    o.console_port = options.console_port;
    o.console_write = options.console_write;
    o.changed = [this](const baker::Config& c) {
        if (changed_) changed_(c);
    };
    o.log = [this](const baker::Error& e) { log_.emplace_back(baker::to_string(e)); };
    return static_cast<bool>(live_.open(o));
}

void bind_live_loop(baker::Context& ctx, std::function<std::string()> report) {
    // The loop's own numbers, for the console and the agent (reading-a-studio.md §7, step 2).
    if (auto f = ctx.func("live.loop", [report = std::move(report)] { return report(); }))
        f->doc(
            "Edit-to-see and reload times per file and layer (Config::loop_stats), as text: "
            "the edits a person saved and the frame that showed each.");
}

std::string loop_report(const baker::Config& cfg) {
    const baker::LoopStats s = cfg.loop_stats();
    auto ms = [](uint64_t ns) { return double(ns) / 1e6; };
    char buf[256];
    std::snprintf(buf, sizeof buf, "%u reloads, %u unchanged, %u failed", s.reloads, s.unchanged,
                  s.failures);
    std::string out = buf;
    for (const auto* rows : {&s.files, &s.layers})
        for (const baker::LoopRow& r : *rows) {
            std::snprintf(buf, sizeof buf,
                          "; %s: %u edits, edit-to-see median %.1f ms worst %.1f ms, reload "
                          "median %.2f ms worst %.2f ms",
                          std::string(r.name.str()).c_str(), r.edits, ms(r.edit_median_ns),
                          ms(r.edit_worst_ns), ms(r.reload_median_ns), ms(r.reload_worst_ns));
            out += buf;
        }
    return out;
}

bool Live::take_log(std::vector<std::string>& out) {
    if (log_.empty()) return false;
    for (std::string& line : log_) out.push_back(std::move(line));
    log_.clear();
    return true;
}

}  // namespace base
