#include "base/live.hpp"

#include <baker/shape.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <filesystem>
#include <utility>

namespace base {

Live::Live(baker::Context& ctx) : ctx_(ctx) {
    cfg_.set_warning_handler(
        [this](const baker::Error& e) { log(std::string(baker::to_string(e))); });
    cfg_.set_time_source([](void*) { return file_clock_now_ns(); });
    cfg_.on_reload_complete([this](const baker::ReloadRecord& r) {
        if (r.outcome == baker::ReloadOutcome::Ok) stack_changed_ = true;
    });
}

bool Live::open(const LiveOptions& options) {
    data_dir_ = options.data_dir;
    changed_ = options.changed;
    baker::LoadSession& session = cfg_.session();
    session.set_pack_levels({"engine", "game", "task"});
    session.add_include_dir(data_dir_);
    cfg_.add_validator(baker::shape::validator(cfg_));
    cfg_.add_validator(baker::check_validator(ctx_, {.packs = &cfg_.read_session()}));

    struct Layer {
        const char* name;
        const char* file;
        bool optional;
    };
    for (const Layer& l : {Layer{"default", "default.dta", false},
                           Layer{"local", "local.dta", true}, Layer{"agent", "agent.dta", true}}) {
        if (!options.local && std::string_view(l.name) == "local") continue;
        if (auto r = cfg_.push_layer(l.name, data_dir_ + "/" + l.file, {.optional = l.optional});
            !r) {
            log(std::string(baker::to_string(r.error())));
            return false;
        }
    }
    // The loop's own numbers, for the console and the agent (reading-a-studio.md §7, step 2).
    if (auto f = ctx_.func("live.loop", [this] { return loop_report(); }))
        f->doc(
            "Edit-to-see and reload times per file and layer (Config::loop_stats), as text: "
            "the edits a person saved and the frame that showed each.");
    // The Config's tools (config.explain, config.set, ...), for the console and the agent.
    // Writing to files stays with the person at the panel: no config.write_back here.
    if (auto r = baker::bind_config_tools(ctx_, cfg_); !r)
        log(std::string(baker::to_string(r.error())));
    autorun();
    if (options.watch) {
        watching_ = watcher_.watch(data_dir_);
        if (!watching_) log("watcher: " + watcher_.error() + "; edits will not reload");
        std::error_code ec;
        const std::filesystem::path packs =
            std::filesystem::path(data_dir_).parent_path() / "packs";
        if (watching_ && std::filesystem::is_directory(packs, ec) && !watcher_.watch(packs))
            log("watcher: " + watcher_.error() + "; pack edits will not reload");
    }
    return true;
}

void Live::begin_frame() {
    if (watching_ && watcher_.poll(changes_)) {
        auto r = cfg_.reload({.trigger = "watcher", .origin_ns = changes_.newest_write_ns});
        if (!r) log(std::string(baker::to_string(r.error())));
    }
    if (stack_changed_) autorun();
}

void Live::autorun() {
    stack_changed_ = false;
    // What a removed layer defined goes with it (D146).
    for (const baker::Error& e : ctx_.run_autorun(cfg_.autorun().items()))
        log(std::string(baker::describe(e)));
    if (changed_) changed_(cfg_);
}

bool Live::reload_now() {
    auto r = cfg_.reload({.trigger = "reload"});
    if (!r) {
        log(std::string(baker::to_string(r.error())));
        return false;
    }
    autorun();
    return *r;
}

std::string Live::loop_report() const {
    const baker::LoopStats s = cfg_.loop_stats();
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

void Live::log(std::string line) {
    log_.push_back(std::move(line));
}

}  // namespace base
