// The headless simulation (stacks/agent.md): a Game's own fixed steps, without a window,
// as fast as the CPU allows, from a scenario; then the objectives judge the run.
//
//   (scenario turn (seconds 20) (start {cube.reset}) (at 5 {...}) (objectives cube.turns))
//   (objective cube.turns (measure {cube.turns}) (pass {>= $value 2}))
//   (objective duel.normal_vs_combo (over median) (measure ...) (pass ...))
//
// An objective `(over median)` is judged once per scenario, by its pass command on the
// median of its values over the seeds run: a measure that varies from seed to seed (a win
// rate) is judged by where it sits, not by its worst seed. Each run still records its own
// value and pass, but only the median's verdict decides whether a set of runs passed.
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
    baker::Node measure;       // a command, evaluated after the run; its result is $value
    baker::Node pass;          // a command; nonzero passes
    bool over_median = false;  // (over median): judged by its median over each scenario's seeds
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
    std::string error;         // the measure or pass command failed; the objective fails
    bool over_median = false;  // its pass here is for information; the median's decides
};

struct RunResult {
    std::string scenario;
    uint64_t seed = 0;
    uint64_t steps = 0;
    std::vector<ObjectiveResult> objectives;
    std::vector<std::string> errors;  // a command of the scenario failed

    // No error, and every objective passed but those judged by their median.
    [[nodiscard]] bool passed() const noexcept;
};

// An `(over median)` objective's verdict on one scenario: its pass command on the median of
// its values over the scenario's runs.
struct MedianResult {
    std::string scenario, objective;
    double value = 0;
    int seeds = 0;  // the values it is the median of
    bool pass = false;
    std::string error;  // no value to judge, or the pass command failed; it fails
};

// Resets the game with `seed` ($seed in script), runs the scenario's start commands, then
// steps it for its seconds at `step_hz`, ticking the Context's scheduler with each step
// and running each `at` when its time comes; then judges it by the scenario's objectives.
[[nodiscard]] RunResult simulate(Game& game, baker::Context& ctx, const Scenario& scenario,
                                 const std::vector<Objective>& objectives, uint64_t seed,
                                 double step_hz);

// Judges each `(over median)` objective on each scenario of `runs`, in scenario then
// objective order, evaluating its pass command in `ctx` with $value the median.
[[nodiscard]] std::vector<MedianResult> judge_medians(baker::Context& ctx,
                                                      const std::vector<Objective>& objectives,
                                                      const std::vector<RunResult>& runs);
// Every run passed, and every median.
[[nodiscard]] bool all_passed(const std::vector<RunResult>& runs,
                              const std::vector<MedianResult>& medians);

// Results as data: one run, the medians' verdicts, and a summary of many, per objective.
[[nodiscard]] std::string to_dta(const RunResult& run);
[[nodiscard]] std::string medians_dta(const std::vector<MedianResult>& medians);
[[nodiscard]] std::string summary_dta(const std::vector<RunResult>& runs,
                                      const std::vector<MedianResult>& medians);

// The same results as one JSON document, schema stacks.sim/1.1 (stacks/agent.md): every
// run, sorted by scenario then seed, its objectives by name; the medians' verdicts; the
// summary per objective; and the status the exit code means. A value that is not finite is
// null, its reason in `error`.
[[nodiscard]] std::string results_json(std::vector<RunResult> runs,
                                       const std::vector<MedianResult>& medians);
// Reads the run-*.dta files, and medians.dta if there is one, that `sim --out` wrote into
// `dir`; false, with `error`, if it cannot.
[[nodiscard]] bool read_runs(const std::filesystem::path& dir, std::vector<RunResult>& out,
                             std::vector<MedianResult>& medians, std::string& error);
// What moved between two sets of runs, schema stacks.compare/1.1: per objective, whether it
// broke, was fixed, stayed the same, or is new or gone; each side's tally; the median's
// change; and what changed. An objective broke when it passed on a scenario and seed and
// now fails, or, judged by its median, when its median passed on a scenario and now fails.
// `broke` is set when anything broke.
[[nodiscard]] std::string compare_json(const std::vector<RunResult>& before,
                                       const std::vector<MedianResult>& medians_before,
                                       const std::vector<RunResult>& after,
                                       const std::vector<MedianResult>& medians_after, bool& broke);

}  // namespace base
