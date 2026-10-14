// The headless simulation (stacks/agent.md): a Game's own fixed steps, without a window,
// as fast as the CPU allows, from a scenario; then the objectives judge the run.
//
//   (scenario turn (seconds 20) (start {cube.reset}) (at 5 {...}) (objectives cube.turns))
//   (objective cube.turns (measure {cube.turns}) (pass {>= $value 2}))
//
// Nothing reads the wall clock: a run is a function of the data, the scenario, and the seed.
#pragma once

#include "base/game.hpp"

#include <baker/array.hpp>
#include <baker/script.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace base {

struct Objective {
    std::string name, desc, file;
    baker::Node measure;  // a command, evaluated after the run; its result is $value
    baker::Node pass;     // a command; nonzero passes
};

struct Scenario {
    struct At {
        double seconds = 0;
        std::vector<baker::Node> commands;
    };
    std::string name, desc, file;
    double seconds = 10;
    std::vector<baker::Node> start;       // before the first step
    std::vector<At> at;                   // in time order
    std::vector<std::string> objectives;  // empty: every objective
    baker::Ref<baker::Array> tree;        // keeps the commands alive
};

// Reads every `(objective ...)` in each .dta file of `dir`, in file-name order, and every
// `(scenario ...)` in `file`. False, with `error` set, on a parse error or a malformed form.
[[nodiscard]] bool load_objectives(const std::filesystem::path& dir, std::vector<Objective>& out,
                                   std::vector<baker::Ref<baker::Array>>& trees,
                                   std::string& error);
[[nodiscard]] bool load_scenarios(const std::filesystem::path& file, std::vector<Scenario>& out,
                                  std::string& error);

struct ObjectiveResult {
    std::string name;
    double value = 0;
    bool pass = false;
    std::string error;  // the measure or pass command failed; the objective fails
};

struct RunResult {
    std::string scenario;
    uint64_t seed = 0;
    uint64_t steps = 0;
    std::vector<ObjectiveResult> objectives;
    std::vector<std::string> errors;  // a command of the scenario failed

    [[nodiscard]] bool passed() const noexcept;
};

// Resets the game with `seed` ($seed in script), runs the scenario's start commands, then
// steps it for its seconds at `step_hz`, ticking the Context's scheduler with each step
// and running each `at` when its time comes; then judges it by the scenario's objectives.
[[nodiscard]] RunResult simulate(Game& game, baker::Context& ctx, const Scenario& scenario,
                                 const std::vector<Objective>& objectives, uint64_t seed,
                                 double step_hz);

// Results as data: one run, and a summary of many, per objective.
[[nodiscard]] std::string to_dta(const RunResult& run);
[[nodiscard]] std::string summary_dta(const std::vector<RunResult>& runs);

}  // namespace base
