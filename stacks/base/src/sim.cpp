#include "base/sim.hpp"

#include "base/fixed_step.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>
#include <baker/task.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

namespace fs = std::filesystem;

namespace base {
namespace {

std::string described(const baker::Error& e) {
    std::string s(baker::describe(e));
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// The commands among `form`'s values from index `from` on.
void commands(const baker::Array& form, size_t from, std::vector<baker::Node>& out) {
    for (size_t i = from; i < form.size(); ++i)
        if (form[i].type() == baker::Type::Command) out.push_back(form[i]);
}

bool number(const baker::Node& n, double& out) {
    auto v = n.as_float();
    if (!v) return false;
    out = *v;
    return true;
}

// The form's name: its second value, a symbol. (objective NAME ...), (scenario NAME ...)
bool form_name(const baker::Array& form, std::string& out) {
    if (form.size() < 2) return false;
    auto s = form[1].as_sym();
    if (!s) return false;
    out = std::string(s->str());
    return true;
}

std::string text_of(const baker::Array& form, const char* tag) {
    if (const baker::Array* a = form.try_find(tag); a && a->size() > 1)
        if (auto s = (*a)[1].as_str()) return std::string(*s);
    return {};
}

// A string literal in the data language, which has two escapes: \q for a double quote and
// \n for a newline. A backslash is written as a slash, so a Windows path cannot form either.
std::string quote(const std::string& text) {
    std::string s = "\"";
    for (char c : text) {
        if (c == '"')
            s += "\\q";
        else if (c == '\n')
            s += "\\n";
        else
            s += c == '\\' ? '/' : c;
    }
    return s + "\"";
}

std::string fixed(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

}  // namespace

bool load_objectives(const fs::path& dir, std::vector<Objective>& out,
                     std::vector<baker::Ref<baker::Array>>& trees, std::string& error) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return true;  // no metrics: nothing to judge by
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.path().extension() == ".dta") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        auto tree = baker::read_file(file.string());
        if (!tree) {
            error = described(tree.error());
            return false;
        }
        for (const baker::Array& form : (*tree)->subarrays()) {
            if (form.empty() || !form[0].is(baker::Symbol("objective"))) continue;
            Objective o;
            o.file = file.string();
            const baker::Array* measure = form.try_find("measure");
            const baker::Array* pass = form.try_find("pass");
            if (!form_name(form, o.name) || !measure || measure->size() < 2 || !pass ||
                pass->size() < 2) {
                error = file.string() +
                        ": an objective needs a name, (measure {command}), and (pass {command})";
                return false;
            }
            o.desc = text_of(form, "desc");
            o.measure = (*measure)[1];
            o.pass = (*pass)[1];
            out.push_back(std::move(o));
        }
        trees.push_back(*tree);
    }
    return true;
}

bool load_scenarios(const fs::path& file, std::vector<Scenario>& out, std::string& error) {
    auto tree = baker::read_file(file.string());
    if (!tree) {
        error = described(tree.error());
        return false;
    }
    for (const baker::Array& form : (*tree)->subarrays()) {
        if (form.empty() || !form[0].is(baker::Symbol("scenario"))) continue;
        Scenario s;
        s.file = file.string();
        s.tree = *tree;
        if (!form_name(form, s.name)) {
            error = file.string() + ": a scenario needs a name: (scenario NAME ...)";
            return false;
        }
        s.desc = text_of(form, "desc");
        if (const baker::Array* sec = form.try_find("seconds");
            !sec || sec->size() < 2 || !number((*sec)[1], s.seconds) || s.seconds <= 0) {
            error = file.string() + ": scenario " + s.name + " needs (seconds N), N above 0";
            return false;
        }
        for (const baker::Array& part : form.subarrays()) {
            if (part.empty()) continue;
            if (part[0].is(baker::Symbol("start"))) {
                commands(part, 1, s.start);
            } else if (part[0].is(baker::Symbol("at"))) {
                Scenario::At at;
                if (part.size() < 2 || !number(part[1], at.seconds)) {
                    error = file.string() + ": scenario " + s.name + ": (at SECONDS {command}...)";
                    return false;
                }
                commands(part, 2, at.commands);
                s.at.push_back(std::move(at));
            } else if (part[0].is(baker::Symbol("objectives"))) {
                for (size_t i = 1; i < part.size(); ++i)
                    if (auto sym = part[i].as_sym()) s.objectives.emplace_back(sym->str());
            }
        }
        std::stable_sort(
            s.at.begin(), s.at.end(),
            [](const Scenario::At& a, const Scenario::At& b) { return a.seconds < b.seconds; });
        out.push_back(std::move(s));
    }
    return true;
}

bool RunResult::passed() const noexcept {
    if (!errors.empty()) return false;
    for (const ObjectiveResult& o : objectives)
        if (!o.pass) return false;
    return true;
}

RunResult simulate(Game& game, baker::Context& ctx, const Scenario& scenario,
                   const std::vector<Objective>& objectives, uint64_t seed, double step_hz) {
    RunResult run;
    run.scenario = scenario.name;
    run.seed = seed;
    ctx.var("seed") = baker::Node(static_cast<int64_t>(seed));
    game.reset(seed);

    auto run_commands = [&](const std::vector<baker::Node>& cmds) {
        for (const baker::Node& c : cmds)
            if (auto r = ctx.eval(c); !r) run.errors.push_back(described(r.error()));
    };
    run_commands(scenario.start);

    // Steps are counted, not timed: the same scenario takes the same steps everywhere.
    const FixedStep clock{.step_ns = FixedStep::hz(step_hz)};
    const double dt = clock.step_seconds();
    const auto total = static_cast<uint64_t>(scenario.seconds * step_hz + 0.5);
    size_t next_at = 0;
    for (uint64_t i = 0; i < total; ++i) {
        const double now = static_cast<double>(i) * dt;
        while (next_at < scenario.at.size() && scenario.at[next_at].seconds <= now + dt * 0.5)
            run_commands(scenario.at[next_at++].commands);
        game.step(dt);
        ctx.scheduler().tick(dt);
        ++run.steps;
    }

    for (const Objective& o : objectives) {
        if (!scenario.objectives.empty() &&
            std::find(scenario.objectives.begin(), scenario.objectives.end(), o.name) ==
                scenario.objectives.end())
            continue;
        ObjectiveResult r;
        r.name = o.name;
        auto value = ctx.eval(o.measure);
        if (!value) {
            r.error = described(value.error());
        } else if (!number(*value, r.value)) {
            r.error = "measure is not a number: " + baker::to_string(*value);
        } else {
            ctx.var("value") = baker::Node(r.value);
            auto pass = ctx.eval(o.pass);
            double p = 0;
            if (!pass)
                r.error = described(pass.error());
            else if (!number(*pass, p))
                r.error = "pass is not a number: " + baker::to_string(*pass);
            else
                r.pass = p != 0;
        }
        run.objectives.push_back(std::move(r));
    }
    for (const std::string& name : scenario.objectives)
        if (std::none_of(objectives.begin(), objectives.end(),
                         [&](const Objective& o) { return o.name == name; }))
            run.errors.push_back("scenario " + scenario.name + " names objective " + name +
                                 ", which no metrics file defines");
    return run;
}

std::string to_dta(const RunResult& run) {
    std::string s = "(run (scenario " + run.scenario + ") (seed " + std::to_string(run.seed) +
                    ") (steps " + std::to_string(run.steps) + ") (passed " +
                    (run.passed() ? "1" : "0") + ")";
    for (const ObjectiveResult& o : run.objectives) {
        s += "\n   (objective " + o.name + " (value " + fixed(o.value) + ") (pass " +
             (o.pass ? "1" : "0") + ")";
        if (!o.error.empty()) s += " (error " + quote(o.error) + ")";
        s += ")";
    }
    for (const std::string& e : run.errors) s += "\n   (error " + quote(e) + ")";
    return s + ")\n";
}

std::string summary_dta(const std::vector<RunResult>& runs) {
    struct Tally {
        int passed = 0, failed = 0;
        std::vector<double> values;
    };
    std::map<std::string, Tally> by;  // by objective, in name order
    int passed = 0;
    for (const RunResult& r : runs) {
        passed += r.passed() ? 1 : 0;
        for (const ObjectiveResult& o : r.objectives) {
            Tally& t = by[o.name];
            (o.pass ? t.passed : t.failed) += 1;
            if (o.error.empty()) t.values.push_back(o.value);
        }
    }
    std::string s = "(summary (runs " + std::to_string(runs.size()) + ") (passed " +
                    std::to_string(passed) + ") (failed " +
                    std::to_string(static_cast<int>(runs.size()) - passed) + ")";
    for (auto& [name, t] : by) {
        s += "\n   (objective " + name + " (passed " + std::to_string(t.passed) + ") (failed " +
             std::to_string(t.failed) + ")";
        if (!t.values.empty()) {
            std::sort(t.values.begin(), t.values.end());
            s += " (min " + fixed(t.values.front()) + ") (median " +
                 fixed(t.values[t.values.size() / 2]) + ") (max " + fixed(t.values.back()) + ")";
        }
        s += ")";
    }
    return s + ")\n";
}

}  // namespace base
