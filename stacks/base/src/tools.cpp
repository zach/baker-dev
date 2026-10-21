// The agent's commands (stacks/agent.md), run from the game's own binary so they know its
// script functions: Baker's host kit runs check, reference, eval, sim, compare, diff and
// promote (baker::host::run_command); dev-files is the stacks' own.
#include "base/app.hpp"

#include <baker/host/commands.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace base {
namespace {

constexpr int kError = baker::host::kCommandError;  // something could not run

[[noreturn]] void usage(const char* problem) {
    std::fprintf(
        stderr,
        "%s\n"
        "commands:\n"
        "  dev-files                      the files of the layers the project marks\n"
        "                                 dev_only, one a line: what packaging leaves out\n"
        "%.*s"
        "every command also takes --data DIR\n",
        problem, int(baker::host::command_help().size()), baker::host::command_help().data());
    std::exit(kError);
}

// The stacks' one flag, which the kit leaves in CommandLine::rest: --data DIR. Anything else
// there is a mistake.
std::string data_dir(const baker::host::CommandLine& line, std::string data) {
    const auto& rest = line.rest;
    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == "--data" && i + 1 < rest.size())
            data = rest[++i];
        else
            usage(("unknown argument: " + std::string(rest[i])).c_str());
    }
    return data;
}

// What the kit's commands need from the game: its functions, its data, its reset and its
// step. The project names the scenarios and objectives (sim/ and metrics/ beside the data).
// `loaded` is the Config the command opened, once it has: what `{live.loop}` reports.
baker::host::CommandHooks hooks(Game& game, const DataStack& stack, const baker::Config*& loaded) {
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
    h.step = [&game](baker::Context&, double dt) { game.step(dt); };
    h.built = stack.project.empty() ? &stack.built : nullptr;
    h.open.skip.add("local");  // one developer's tuning changes no check or simulation
    return h;
}

// The files of the layers the project marks dev_only, as the project names them, one a line:
// what a package must not carry, read the way the game reads its project.
int dev_files(const std::string& data) {
    const DataStack stack = data_stack(data);
    if (stack.project.empty()) return 0;  // no project file: nothing marked
    auto project = baker::read_project(baker::find_project(stack.project).str());
    if (!project) {
        std::fprintf(stderr, "%s\n", std::string(baker::describe(project.error())).c_str());
        return kError;
    }
    for (const baker::ProjectLayer& l : project->layers)
        if (l.dev_only) std::printf("%s\n", l.path.c_str());
    return 0;
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
    const std::string data = data_dir(*line, resolved.data_dir);
    if (line->command == "dev-files") return dev_files(data);
    const DataStack stack = data_stack(data);
    line->project = stack.project;
    line->patch_root = data;  // a patch applies from the data directory
    const baker::Config* loaded = nullptr;
    const int status = baker::host::run_command(*line, hooks(game, stack, loaded));
    if (status >= 0) return status;
    usage(("unknown command: " + std::string(line->command)).c_str());
}

}  // namespace base
