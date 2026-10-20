// The agent's commands (stacks/agent.md), run from the game's own binary so they know its
// script functions: Baker's host kit runs check, reference, eval, diff and promote
// (baker::host::run_command); sim and compare are the stacks' own.
#include "base/app.hpp"
#include "base/sim.hpp"

#include <baker/host/commands.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace base {
namespace {

constexpr int kFailed = baker::host::kCommandFailed;  // an objective failed, or check found
constexpr int kError = baker::host::kCommandError;    // something could not run

// The stacks' own flags, which the kit leaves in CommandLine::rest.
struct Options {
    std::string data, sim, metrics, out;
    std::vector<std::string> scenarios, dirs;  // dirs: compare's two directories
    uint64_t seed_first = 1, seed_last = 1;
    double step_hz = 60;
    bool json = false;
};

[[noreturn]] void usage(const char* problem) {
    std::fprintf(stderr,
                 "%s\n"
                 "commands:\n"
                 "  sim [--scenario NAME]... [--seeds A-B] [--out DIR] [--step-hz N] [--json]\n"
                 "                                 --out also gets results.json (stacks.sim/1.1);\n"
                 "                                 --json prints it instead of the text\n"
                 "  compare BEFORE_DIR AFTER_DIR   which objectives moved between two --out\n"
                 "                                 directories (stacks.compare/1.1); exit 1 if one "
                 "broke\n"
                 "%.*s"
                 "every command also takes --data DIR, --sim DIR, --metrics DIR\n",
                 problem, int(baker::host::command_help().size()),
                 baker::host::command_help().data());
    std::exit(kError);
}

uint64_t number(std::string_view text) {
    uint64_t v = 0;
    if (text.empty()) usage("bad number");
    for (const char c : text) {
        if (c < '0' || c > '9') usage("bad number");
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    return v;
}

// Reads the stacks' flags from what the kit left; anything else is a mistake.
Options parse(const baker::host::CommandLine& line, const std::string& data) {
    Options o;
    o.data = data;
    o.json = line.json;
    o.seed_first = o.seed_last = line.seed;
    const auto& rest = line.rest;
    for (size_t i = 0; i < rest.size(); ++i) {
        const std::string_view a = rest[i];
        const bool more = i + 1 < rest.size();
        if (a == "--data" && more) {
            o.data = rest[++i];
        } else if (a == "--sim" && more) {
            o.sim = rest[++i];
        } else if (a == "--metrics" && more) {
            o.metrics = rest[++i];
        } else if (a == "--out" && more) {
            o.out = rest[++i];
        } else if (a == "--scenario" && more) {
            o.scenarios.emplace_back(rest[++i]);
        } else if (a == "--seeds" && more) {
            const std::string_view s = rest[++i];
            const size_t dash = s.find('-');
            o.seed_first = number(s.substr(0, dash));
            o.seed_last =
                dash == std::string_view::npos ? o.seed_first : number(s.substr(dash + 1));
            if (o.seed_last < o.seed_first) usage("--seeds A-B needs A <= B");
        } else if (a == "--step-hz" && more) {
            o.step_hz = static_cast<double>(number(rest[++i]));
            if (o.step_hz <= 0) usage("--step-hz must be above 0");
        } else if (line.command == "compare" && !a.empty() && a[0] != '-') {
            o.dirs.emplace_back(a);
        } else {
            usage(("unknown argument: " + std::string(a)).c_str());
        }
    }
    // sim/ and metrics/ sit beside data/ (game-stacks.md §2.4).
    const fs::path parent = fs::path(o.data).parent_path();
    if (o.sim.empty()) o.sim = (parent / "sim").string();
    if (o.metrics.empty()) o.metrics = (parent / "metrics").string();
    return o;
}

// The game's data, as sim sees it: default.dta and agent.dta, without local.dta.
struct Loaded {
    baker::Context ctx;
    Live live{ctx};
    Game* game = nullptr;  // bound to ctx: unbound before it goes
    Loaded() = default;
    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;
    ~Loaded() {
        if (game) game->unbind(ctx);
    }
};

bool load(Game& game, Loaded& l, const Options& o) {
    game.bind(l.ctx);
    l.game = &game;
    const bool ok = l.live.open({.data_dir = o.data,
                                 .watch = false,
                                 .local = false,
                                 .changed = [&game](const baker::Config& c) { game.load(c); }});
    std::vector<std::string> log;
    if (l.live.take_log(log))
        for (const std::string& line : log) std::fprintf(stderr, "%s\n", line.c_str());
    return ok;
}

bool scenario_files(const Options& o, std::vector<fs::path>& files) {
    std::error_code ec;
    if (!fs::is_directory(o.sim, ec)) return false;
    for (const auto& entry : fs::directory_iterator(o.sim, ec))
        if (entry.path().extension() == ".dta") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return true;
}

bool write_file(const fs::path& path, const std::string& text) {
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

int sim(Game& game, const Options& o) {
    Loaded l;
    if (!load(game, l, o)) return kError;
    std::vector<Objective> objectives;
    std::vector<baker::Ref<baker::Array>> metric_trees;
    std::string error;
    if (!load_objectives(o.metrics, objectives, metric_trees, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return kError;
    }
    std::vector<fs::path> files;
    if (!scenario_files(o, files)) {
        std::fprintf(stderr, "no scenario directory: %s\n", o.sim.c_str());
        return kError;
    }
    std::vector<Scenario> scenarios;
    for (const fs::path& f : files)
        if (!load_scenarios(f, scenarios, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return kError;
        }
    // --scenario picks by scenario name or by file name without .dta.
    std::vector<const Scenario*> chosen;
    for (const Scenario& s : scenarios)
        if (o.scenarios.empty() ||
            std::any_of(o.scenarios.begin(), o.scenarios.end(), [&](const std::string& n) {
                return n == s.name || n == fs::path(s.file).stem().string();
            }))
            chosen.push_back(&s);
    if (chosen.empty()) {
        std::fprintf(stderr, "no scenario to run in %s\n", o.sim.c_str());
        return kError;
    }
    std::error_code ec;
    if (!o.out.empty()) fs::create_directories(o.out, ec);

    std::vector<RunResult> runs;
    for (const Scenario* s : chosen)
        for (uint64_t seed = o.seed_first; seed <= o.seed_last; ++seed) {
            RunResult r = simulate(game, l.ctx, *s, objectives, seed, o.step_hz);
            if (!o.json) {
                std::printf("%s seed %llu: %s", s->name.c_str(),
                            static_cast<unsigned long long>(seed),
                            r.passed() ? "passed" : "FAILED");
                for (const ObjectiveResult& obj : r.objectives)
                    std::printf("  %s %g%s", obj.name.c_str(), obj.value,
                                obj.pass ? "" : " (fail)");
                std::printf("\n");
                for (const std::string& e : r.errors) std::printf("  error: %s\n", e.c_str());
                for (const ObjectiveResult& obj : r.objectives)
                    if (!obj.error.empty())
                        std::printf("  %s: %s\n", obj.name.c_str(), obj.error.c_str());
            }
            if (!o.out.empty() && !write_file(fs::path(o.out) / ("run-" + s->name + "-" +
                                                                 std::to_string(seed) + ".dta"),
                                              to_dta(r))) {
                std::fprintf(stderr, "cannot write to %s\n", o.out.c_str());
                return kError;
            }
            runs.push_back(std::move(r));
        }
    // An objective (over median) is judged once per scenario, by its median over the seeds.
    const std::vector<MedianResult> medians = judge_medians(l.ctx, objectives, runs);
    if (!o.json)
        for (const MedianResult& m : medians)
            std::printf("%s median of %d seeds: %s  %s %g%s\n", m.scenario.c_str(), m.seeds,
                        m.pass ? "passed" : "FAILED", m.objective.c_str(), m.value,
                        m.error.empty() ? "" : ("  " + m.error).c_str());
    const std::string summary = summary_dta(runs, medians);
    const std::string json = results_json(runs, medians);
    std::printf("%s", (o.json ? json : summary).c_str());
    if (!o.out.empty() && (!write_file(fs::path(o.out) / "summary.dta", summary) ||
                           !write_file(fs::path(o.out) / "medians.dta", medians_dta(medians)) ||
                           !write_file(fs::path(o.out) / "results.json", json)))
        return kError;
    return all_passed(runs, medians) ? 0 : kFailed;
}

// Which objectives moved between two `sim --out` directories, without running anything.
int compare(const Options& o) {
    if (o.dirs.size() != 2) usage("compare needs two result directories: compare BEFORE AFTER");
    std::vector<RunResult> before, after;
    std::vector<MedianResult> medians_before, medians_after;
    std::string error;
    if (!read_runs(o.dirs[0], before, medians_before, error) ||
        !read_runs(o.dirs[1], after, medians_after, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return kError;
    }
    bool broke = false;
    std::printf("%s", compare_json(before, medians_before, after, medians_after, broke).c_str());
    return broke ? kFailed : 0;
}

// What the kit's commands need from the game: its functions, its data, its reset, and the
// scenarios and objectives check reads beside the data (the agent writes all of them).
// `loaded` is the Config the command opened, once it has: what `{live.loop}` reports.
baker::host::CommandHooks hooks(Game& game, const Options& o, const DataStack& stack,
                                const baker::Config*& loaded) {
    baker::host::CommandHooks h;
    h.bind = [&game, &loaded](baker::Context& ctx) {
        game.bind(ctx);
        bind_live_loop(ctx, [&loaded] { return loaded ? loop_report(*loaded) : std::string(); });
    };
    h.unbind = [&game](baker::Context& ctx) { game.unbind(ctx); };
    h.changed = [&game, &loaded](const baker::Config& c) {
        loaded = &c;
        game.load(c);
    };
    h.reset = [&game](baker::Context&, uint64_t seed) { game.reset(seed); };
    h.extra_trees =
        [&o](const baker::Config&) -> baker::Expected<baker::List<baker::Ref<baker::Array>>> {
        std::vector<baker::Ref<baker::Array>> trees;
        std::vector<fs::path> files;
        scenario_files(o, files);
        for (const fs::path& f : files) {
            auto t = baker::read_file(f.string());
            if (!t) return baker::Unexpected(t.error());
            trees.push_back(*t);
        }
        std::vector<Objective> objectives;
        std::string error;
        if (!load_objectives(o.metrics, objectives, trees, error))
            return baker::Unexpected(baker::make_error(baker::ErrorCode::Syntax, error));
        return baker::List<baker::Ref<baker::Array>>(
            std::span<const baker::Ref<baker::Array>>(trees.data(), trees.size()));
    };
    h.built = stack.project.empty() ? &stack.built : nullptr;
    h.open.skip.add("local");  // one developer's tuning changes no check or simulation
    return h;
}

}  // namespace

int main(Game& game, int argc, char** argv, AppOptions defaults) {
    if (argc < 2 || argv[1][0] == '-')
        return run(game, parse_args(argc, argv, std::move(defaults)));
    auto line = baker::host::parse_command_line(argc, argv);
    if (!line) usage(std::string(baker::to_string(line.error())).c_str());
    // The data directory a play would use (beside the executable when packaged), so the
    // commands see the same data.
    const AppOptions resolved = parse_args(1, argv, std::move(defaults));
    const Options o = parse(*line, resolved.data_dir);
    if (line->command == "sim") return sim(game, o);
    if (line->command == "compare") return compare(o);
    const DataStack stack = data_stack(o.data);
    line->project = stack.project;
    line->patch_root = o.data;  // a patch applies from the data directory
    const baker::Config* loaded = nullptr;
    const int status = baker::host::run_command(*line, hooks(game, o, stack, loaded));
    if (status >= 0) return status;
    usage(("unknown command: " + std::string(line->command)).c_str());
}

}  // namespace base
