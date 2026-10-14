// Live: the edit-to-see loop over Moira, the same in every stack (game-stacks.md §2.2,
// §2.3). It owns the Config, its layer stack, and a watcher on the data directory:
//
//   default   <data>/default.dta   the game
//   local     <data>/local.dta     one developer's own tuning; not in version control
//   agent     <data>/agent.dta     an agent's experiments (§3.3)
//   scratch   (Moira's)            unsaved live edits
//
// After the load, and at the frame after any change to the stack (a reload, a toggle from a
// panel), it runs the layers' #autorun commands, so a data file can define script
// functions: `#autorun {func boss.restart ($n) ...}`; then it calls `changed`, where the game
// reads its structured data (Game::load).
//
// A stack's packs (game-stacks.md §2.4) are in <data>/../packs, which data files #require by
// relative path; the watcher watches that directory too, so editing a pack reloads.
//
// The Config's clock is the file system's, so a reload's origin is the time its file was
// saved, and loop_stats() measures from the save to the frame that showed it.
//
//   base::Live live(ctx);
//   if (!live.open({.data_dir = "data"})) ...  // after binding the game's script functions
//   each frame: live.begin_frame(); ...simulate, draw, present...; live.end_frame();
#pragma once

#include "base/watcher.hpp"

#include <moira/config.hpp>
#include <moira/script.hpp>

#include <functional>
#include <string>
#include <vector>

namespace base {

struct LiveOptions {
    std::string data_dir;  // holds default.dta, and local.dta and agent.dta if present
    bool watch = true;     // false: reload only when asked (tests, the headless runner)
    bool local = true;     // false: leave out local.dta, so one developer's tuning cannot
                           // change a simulation's results or a check's findings
    // Called after the load and after each change to the stack, once #autorun has run.
    std::function<void(const moira::Config&)> changed{};
};

class Live {
public:
    explicit Live(moira::Context& ctx);
    Live(const Live&) = delete;
    Live& operator=(const Live&) = delete;

    // Pushes the layers, adds the shape and check validators, and starts watching. The
    // game's script functions should be bound first, so check knows them. False on failure,
    // with the reason in the log.
    [[nodiscard]] bool open(const LiveOptions& options);

    // Reloads if the watcher saw a data file change. Call before the frame reads tweaks.
    void begin_frame();
    // The frame that will show the last reload is presented: closes edit-to-see.
    void end_frame() noexcept { cfg_.mark_visible(); }
    // Reloads now, whatever the watcher says; true if anything changed.
    bool reload_now();

    [[nodiscard]] moira::Config& config() noexcept { return cfg_; }
    [[nodiscard]] const moira::Config& config() const noexcept { return cfg_; }
    [[nodiscard]] const std::string& data_dir() const noexcept { return data_dir_; }

    // Moves the warnings, validator findings, and reload errors logged since the last call
    // to the end of `out`, oldest first; false if there were none.
    bool take_log(std::vector<std::string>& out);

private:
    void log(std::string line);
    // Runs every enabled layer's #autorun commands (Config::autorun, D107): the {func}s and
    // setup the data asks for, again after each change, so a redefinition takes effect.
    // Then calls changed_.
    void autorun();

    moira::Context& ctx_;
    moira::Config cfg_;
    Watcher watcher_;
    Changes changes_;
    bool watching_ = false;
    bool stack_changed_ = false;  // set by the Config after any change; autorun runs again
    std::string data_dir_;
    std::function<void(const moira::Config&)> changed_;
    std::vector<std::string> log_;
};

}  // namespace base
