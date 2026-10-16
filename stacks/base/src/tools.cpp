// The agent's commands (stacks/agent.md): sim, check, reference, diff, promote, and eval, run
// from the game's own binary so they know its script functions.
#include "base/app.hpp"
#include "base/sim.hpp"

#include <baker/shape.hpp>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>

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

constexpr int kFailed = 1;  // an objective failed, or check found errors
constexpr int kError = 2;   // something could not run

struct Options {
    std::string data, sim, metrics, out, to = "default";
    std::vector<std::string> scenarios, commands;
    uint64_t seed_first = 1, seed_last = 1;
    double step_hz = 60;
    bool write = false;
    bool json = false;
};

[[noreturn]] void usage(const char* problem) {
    std::fprintf(
        stderr,
        "%s\n"
        "commands:\n"
        "  sim [--scenario NAME]... [--seeds A-B] [--out DIR] [--step-hz N]\n"
        "  check                          baker.check/1.1 JSON; exit 1 on an error, a load\n"
        "                                 problem, a shape error, or an undefined function\n"
        "  reference [--json]             the game's script vocabulary, as Markdown; --json:\n"
        "                                 {reference, tweaks, shapes} as Baker's JSON\n"
        "  diff                           what agent.dta changes\n"
        "  promote [--to LAYER] [--write] agent.dta into LAYER (default); --write writes\n"
        "  eval [--seed N] \"{command}\"...   each command's result, one a line, on the\n"
        "                                 game reset with the seed (default 1)\n"
        "every command also takes --data DIR, --sim DIR, --metrics DIR\n",
        problem);
    std::exit(kError);
}

uint64_t number(const char* text) {
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text, &end, 10);
    if (!*text || *end) usage("bad number");
    return v;
}

Options parse(int argc, char** argv, const std::string& data) {
    Options o;
    o.data = data;
    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        const bool more = i + 1 < argc;
        if (!std::strcmp(a, "--json")) {
            o.json = true;
        } else if (!std::strcmp(a, "--data") && more) {
            o.data = argv[++i];
        } else if (!std::strcmp(a, "--sim") && more) {
            o.sim = argv[++i];
        } else if (!std::strcmp(a, "--metrics") && more) {
            o.metrics = argv[++i];
        } else if (!std::strcmp(a, "--out") && more) {
            o.out = argv[++i];
        } else if (!std::strcmp(a, "--scenario") && more) {
            o.scenarios.emplace_back(argv[++i]);
        } else if (!std::strcmp(a, "--seed") && more) {
            o.seed_first = o.seed_last = number(argv[++i]);
        } else if (!std::strcmp(a, "--seeds") && more) {
            const std::string s = argv[++i];
            const size_t dash = s.find('-');
            o.seed_first = number(s.substr(0, dash).c_str());
            o.seed_last =
                dash == std::string::npos ? o.seed_first : number(s.substr(dash + 1).c_str());
            if (o.seed_last < o.seed_first) usage("--seeds A-B needs A <= B");
        } else if (!std::strcmp(a, "--step-hz") && more) {
            o.step_hz = static_cast<double>(number(argv[++i]));
            if (o.step_hz <= 0) usage("--step-hz must be above 0");
        } else if (!std::strcmp(a, "--to") && more) {
            o.to = argv[++i];
        } else if (!std::strcmp(a, "--write")) {
            o.write = true;
        } else if (a[0] == '{') {
            o.commands.emplace_back(a);  // eval's commands
        } else {
            usage("unknown argument");
        }
    }
    // sim/ and metrics/ sit beside data/ (game-stacks.md §2.4).
    const fs::path parent = fs::path(o.data).parent_path();
    if (o.sim.empty()) o.sim = (parent / "sim").string();
    if (o.metrics.empty()) o.metrics = (parent / "metrics").string();
    return o;
}

// The game's data, as the commands see it: default.dta and agent.dta, without local.dta.
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
            std::printf("%s seed %llu: %s", s->name.c_str(), static_cast<unsigned long long>(seed),
                        r.passed() ? "passed" : "FAILED");
            for (const ObjectiveResult& obj : r.objectives)
                std::printf("  %s %g%s", obj.name.c_str(), obj.value, obj.pass ? "" : " (fail)");
            std::printf("\n");
            for (const std::string& e : r.errors) std::printf("  error: %s\n", e.c_str());
            for (const ObjectiveResult& obj : r.objectives)
                if (!obj.error.empty())
                    std::printf("  %s: %s\n", obj.name.c_str(), obj.error.c_str());
            if (!o.out.empty() && !write_file(fs::path(o.out) / ("run-" + s->name + "-" +
                                                                 std::to_string(seed) + ".dta"),
                                              to_dta(r))) {
                std::fprintf(stderr, "cannot write to %s\n", o.out.c_str());
                return kError;
            }
            runs.push_back(std::move(r));
        }
    const std::string summary = summary_dta(runs);
    std::printf("%s", summary.c_str());
    if (!o.out.empty() && !write_file(fs::path(o.out) / "summary.dta", summary)) return kError;
    return std::all_of(runs.begin(), runs.end(), [](const RunResult& r) { return r.passed(); })
               ? 0
               : kFailed;
}

// Prints check's JSON (baker.check/1.1) with the load problems, and returns the exit status.
int report_check(baker::CheckReport& report, const Loaded& l, const baker::LoadSession* packs) {
    const auto& load = l.live.load_errors();
    report.load =
        baker::List<baker::Error>(std::span<const baker::Error>(load.data(), load.size()));
    std::printf("%s\n", baker::to_json(report, packs).c_str());
    return report.ok() ? 0 : kFailed;
}

int check(Game& game, const Options& o) {
    Loaded l;
    baker::CheckReport report;
    // A call to a function nobody defined is demand to Baker: a word the data asks for. In
    // an agent's edit it is nearly always a typo, so it fails the check unless declared a
    // stub ({func name () (status stub) ...}), which is how a push-up is written.
    report.unresolved_fails = true;
    // Data that does not load is still a report: the JSON says why, and the exit status
    // stays "could not load".
    if (!load(game, l, o)) {
        report_check(report, l, nullptr);
        return kError;
    }
    const baker::Config& cfg = l.live.config();
    // The merged data and its #autorun script, every scenario, and every objective: the
    // agent writes all of them.
    std::vector<baker::Ref<baker::Array>> trees;
    std::vector<fs::path> files;
    scenario_files(o, files);
    for (const fs::path& f : files) {
        auto t = baker::read_file(f.string());
        if (!t) {
            report.load = baker::List<baker::Error>(std::span<const baker::Error>(&t.error(), 1));
            std::printf("%s\n", baker::to_json(report, &cfg.read_session()).c_str());
            std::fprintf(stderr, "%s\n", std::string(baker::describe(t.error())).c_str());
            return kError;
        }
        trees.push_back(*t);
    }
    std::vector<Objective> objectives;
    std::string error;
    if (!load_objectives(o.metrics, objectives, trees, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return kError;
    }
    std::vector<const baker::Array*> extra;
    for (const auto& t : trees) extra.push_back(t.get());
    baker::check(cfg, l.ctx, report, {.unresolved_fails = true},
                 std::span<const baker::Array* const>(extra.data(), extra.size()));
    for (const baker::CheckReport::Demand& d : report.demand)
        if (!d.stub)
            std::fprintf(stderr, "check: '%s' is not defined (%s:%d)\n", d.name.c_str(),
                         d.sites.empty() ? "?" : d.sites.begin()->file.c_str(),
                         d.sites.empty() ? 0 : int(d.sites.begin()->line));
    // The shapes, over the merged data: a value out of range or of the wrong type fails the
    // check; a field no shape declares is reported, as demand for a word, and passes.
    baker::shape::Set shapes;
    baker::shape::Report shaped;
    shapes.add(*cfg.root(), shaped);
    baker::shape::check(*cfg.root(), shapes, shaped);
    for (const baker::Error& e : shaped.errors)
        std::fprintf(stderr, "shape: %s\n", std::string(baker::to_string(e)).c_str());
    for (const baker::Error& e : shaped.warnings)
        std::fprintf(stderr, "shape warning: %s\n", std::string(baker::to_string(e)).c_str());
    report.shape_errors = baker::List<baker::Error>(shaped.errors.items());
    report.shape_warnings = baker::List<baker::Error>(shaped.warnings.items());
    return report_check(report, l, &cfg.read_session());
}

// Commands over the game's data and its reset state, as a console would run them, without
// a window: what a frame table or a search says, before and after an edit.
int eval(Game& game, const Options& o) {
    if (o.commands.empty()) usage("eval needs a command: eval \"{...}\"");
    Loaded l;
    if (!load(game, l, o)) return kError;
    l.ctx.var("seed") = baker::Node(static_cast<int64_t>(o.seed_first));
    game.reset(o.seed_first);
    int status = 0;
    for (const std::string& command : o.commands) {
        auto r = l.ctx.eval(command);
        if (!r) {
            std::fprintf(stderr, "%s\n", std::string(baker::describe(r.error())).c_str());
            status = kFailed;
            continue;
        }
        if (auto text = r->as_str())
            std::printf("%.*s\n", int(text->size()), text->data());
        else
            std::printf("%s\n", baker::to_string(*r).c_str());
    }
    return status;
}

int reference(Game& game, const Options& o) {
    Loaded l;
    if (!load(game, l, o)) return kError;
    const baker::Config& cfg = l.live.config();
    if (!o.json) {
        std::printf("%s", baker::reference(l.ctx, &cfg.read_session()).c_str());
        return 0;
    }
    // The three documents in one object: the vocabulary, the tweaks, and the shapes the
    // enabled layers declare (Baker's docs/reference-json.md).
    std::string ref, tweaks, shapes;
    baker::write_reference_json(l.ctx, baker::string_out(ref), &cfg.read_session());
    baker::write_json(baker::TweakRegistry::global(), baker::string_out(tweaks));
    baker::shape::Set set;
    baker::shape::Report report;
    for (const baker::Symbol layer : cfg.layers())
        if (cfg.layer_enabled(layer.str()))
            if (const baker::Ref<baker::Array> root = cfg.layer_root(layer.str()))
                set.add(*root, report, &cfg.read_session());
    baker::shape::write_json(set, baker::string_out(shapes));
    std::printf("{\"reference\": %s, \"tweaks\": %s, \"shapes\": %s}\n", ref.c_str(),
                tweaks.c_str(), shapes.c_str());
    return 0;
}

// One line per value the agent layer sets: path, the value below it, its value, where.
int print_effect(baker::Config& cfg, const char* heading) {
    auto effect = cfg.layer_effect("agent");
    if (!effect) {
        std::fprintf(stderr, "%s\n", std::string(baker::to_string(effect.error())).c_str());
        return kError;
    }
    if (effect->empty()) {
        std::printf("agent.dta changes nothing\n");
        return 0;
    }
    std::printf("%s\n", heading);
    for (const baker::LayerEffectEntry& e : *effect) {
        const std::string below = e.below ? baker::to_string(*e.below) : std::string("(new)");
        std::printf("  %s  %s -> %s  (%s:%d)%s\n", e.path.c_str(), below.c_str(),
                    baker::to_string(e.value).c_str(), e.source.file.c_str(), int(e.source.line),
                    e.shadowed ? "  shadowed" : "");
    }
    return 0;
}

int diff(Game& game, const Options& o) {
    Loaded l;
    if (!load(game, l, o)) return kError;
    return print_effect(l.live.config(), "agent.dta changes:");
}

int promote(Game& game, const Options& o) {
    Loaded l;
    if (!load(game, l, o)) return kError;
    if (!o.write) {
        const std::string heading = "would write into " + o.to + " (rerun with --write to write):";
        return print_effect(l.live.config(), heading.c_str());
    }
    auto report = l.live.config().promote("agent", o.to);
    if (!report) {
        std::fprintf(stderr, "%s\n", std::string(baker::describe(report.error())).c_str());
        return kError;
    }
    for (const baker::Error& e : report->refused)
        std::fprintf(stderr, "refused: %s\n", std::string(baker::to_string(e)).c_str());
    if (!report->ok()) return kFailed;
    for (const baker::WrittenLine& w : report->written)
        std::printf("wrote %s:%d\n", w.file.c_str(), int(w.line));
    std::printf("agent.dta is unchanged: empty it once the promotion is reviewed\n");
    return 0;
}

}  // namespace

int main(Game& game, int argc, char** argv, AppOptions defaults) {
    if (argc < 2 || argv[1][0] == '-')
        return run(game, parse_args(argc, argv, std::move(defaults)));
    // The data directory a play would use (beside the executable when packaged), so the
    // commands see the same data.
    const AppOptions resolved = parse_args(1, argv, std::move(defaults));
    const std::string command = argv[1];
    const Options o = parse(argc, argv, resolved.data_dir);
    if (command == "sim") return sim(game, o);
    if (command == "check") return check(game, o);
    if (command == "reference") return reference(game, o);
    if (command == "diff") return diff(game, o);
    if (command == "promote") return promote(game, o);
    if (command == "eval") return eval(game, o);
    usage(("unknown command: " + command).c_str());
}

}  // namespace base
