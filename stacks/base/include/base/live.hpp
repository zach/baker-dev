// Live: the stacks' edit-to-see loop (game-stacks.md §2.2, §2.3), Baker's host kit
// (baker::host::Live) with the stacks' conventions. The stack:
//
//   default   <data>/default.dta   the game
//   local     <data>/local.dta     one developer's own tuning; not in version control
//   agent     <data>/agent.dta     an agent's experiments (§3.3)
//   scratch   (Baker's)            unsaved live edits
//
// is described by <data>/../baker-project.dta, which an editor or a checker reads too
// (Baker's read_project); without one, as in a packaged game, the stack above with pack
// levels engine, game, and task, and <data> as an include directory. Packs (§2.4) are in
// <data>/../packs, which data files #require by relative path.
//
// The kit does the rest: it watches every file the stack read (packs and includes too),
// reloads with the save's time as the origin, runs the layers' #autorun commands after the
// load and after any change to the stack, then calls `changed`, where the game reads its
// structured data (Game::load); it serves the remote console. What the stacks add: the
// layer convention, leaving out local.dta for a simulation or a check, `{live.loop}`, and
// the log as lines a panel shows.
//
//   base::Live live(ctx);
//   if (!live.open({.data_dir = "data"})) ...  // after binding the game's script functions
//   each frame: live.begin_frame(); ...simulate, draw, present...; live.end_frame();
#pragma once

#include <baker/config.hpp>
#include <baker/host/live.hpp>
#include <baker/project.hpp>
#include <baker/script.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace base {

// The stack a data directory names: `project`, where the kit finds baker-project.dta from
// (the data directory, when the file is beside it; empty when there is none), and `built`,
// the stack above, for when there is none.
struct DataStack {
    std::string project;
    baker::Project built;
};
[[nodiscard]] DataStack data_stack(const std::string& data_dir);

// Config::loop_stats() as one line of text: what `{live.loop}` returns.
[[nodiscard]] std::string loop_report(const baker::Config& cfg);
// Binds `{live.loop}` to `report`: the stacks' word for the loop's own numbers, which the
// game's vocabulary has whether it plays or runs a command.
void bind_live_loop(baker::Context& ctx, std::function<std::string()> report);

struct LiveOptions {
    std::string data_dir;       // holds default.dta, and local.dta and agent.dta if present
    bool watch = true;          // false: reload only when asked (tests, the headless runner)
    bool local = true;          // false: leave out local.dta, so one developer's tuning cannot
                                // change a simulation's results or a check's findings
    uint16_t console_port = 0;  // the remote console on 127.0.0.1; 0: none
    // Also bind config.write_back and config.promote for the console, so a tool (an
    // editor's "write the live value here") can write a live edit to its line. Off unless
    // the person starting the game asks (--console-write): writing files is otherwise the
    // panel's.
    bool console_write = false;
    // Called after the load and after each change to the stack, once #autorun has run, and
    // after a live edit.
    std::function<void(const baker::Config&)> changed{};
};

class Live {
public:
    explicit Live(baker::Context& ctx) : ctx_(ctx), live_(ctx) {}
    Live(const Live&) = delete;
    Live& operator=(const Live&) = delete;

    // Opens the stack, adds the shape and check validators, and starts watching and the
    // console. The game's script functions should be bound first, so check knows them.
    // False on failure, with the reason in the log.
    [[nodiscard]] bool open(const LiveOptions& options);

    // Reloads if a data file was saved, and answers the console. Call before the frame
    // reads tweaks.
    void begin_frame() { live_.begin_frame(); }
    // The frame that will show the last reload is presented: closes edit-to-see.
    void end_frame() noexcept { live_.end_frame(); }
    // Reloads now, whatever the watcher says; true if anything changed.
    bool reload_now() { return live_.reload_now(); }

    [[nodiscard]] baker::Config& config() noexcept { return live_.config(); }
    [[nodiscard]] const baker::Config& config() const noexcept { return live_.config(); }
    [[nodiscard]] const std::string& data_dir() const noexcept { return data_dir_; }
    // The console's port, once open() started one; 0 if none.
    [[nodiscard]] uint16_t console_port() const noexcept { return live_.console_port(); }

    // Moves the warnings, validator findings, and reload errors logged since the last call
    // to the end of `out`, oldest first; false if there were none.
    bool take_log(std::vector<std::string>& out);
    // The located problems the last load reported, which the game reports too: a layer that
    // did not read or parse, and each #autorun command that failed. For the check gate.
    [[nodiscard]] std::span<const baker::Error> load_errors() const noexcept {
        return live_.load_errors();
    }

private:
    baker::Context& ctx_;
    baker::host::Live live_;
    std::string data_dir_;
    std::function<void(const baker::Config&)> changed_;
    std::vector<std::string> log_;
};

}  // namespace base
