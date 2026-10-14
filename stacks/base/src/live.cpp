#include "base/live.hpp"

#include <moira/shape.hpp>
#include <moira/std_interop.hpp>

#include <filesystem>
#include <utility>

namespace base {

Live::Live(moira::Context& ctx) : ctx_(ctx) {
    cfg_.set_warning_handler(
        [this](const moira::Error& e) { log(std::string(moira::to_string(e))); });
    cfg_.set_time_source([](void*) { return file_clock_now_ns(); });
    cfg_.on_reload_complete([this](const moira::ReloadRecord& r) {
        if (r.outcome == moira::ReloadOutcome::Ok) stack_changed_ = true;
    });
}

bool Live::open(const LiveOptions& options) {
    data_dir_ = options.data_dir;
    changed_ = options.changed;
    moira::LoadSession& session = cfg_.session();
    session.set_pack_levels({"engine", "game", "task"});
    session.add_include_dir(data_dir_);
    cfg_.add_validator(moira::shape::validator());
    cfg_.add_validator(moira::check_validator(ctx_, {.packs = &cfg_.read_session()}));

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
            log(std::string(moira::to_string(r.error())));
            return false;
        }
    }
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
        if (!r) log(std::string(moira::to_string(r.error())));
    }
    if (stack_changed_) autorun();
}

void Live::autorun() {
    stack_changed_ = false;
    for (const moira::Node& command : cfg_.autorun())
        if (auto r = ctx_.eval(command); !r) log(std::string(moira::describe(r.error())));
    if (changed_) changed_(cfg_);
}

bool Live::reload_now() {
    auto r = cfg_.reload({.trigger = "reload"});
    if (!r) {
        log(std::string(moira::to_string(r.error())));
        return false;
    }
    autorun();
    return *r;
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
