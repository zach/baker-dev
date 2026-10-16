#include "base/sim.hpp"

#include "base/fixed_step.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/std_script.hpp>
#include <baker/task.hpp>

#include <algorithm>
#include <cmath>
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

// --- JSON (stacks.sim/1, stacks.compare/1) ---

namespace {

std::string json_str(const std::string& text) {
    std::string s = "\"";
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            s += '\\';
            s += ch;
        } else if (c == '\n') {
            s += "\\n";
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            s += buf;
        } else {
            s += ch;
        }
    }
    return s + "\"";
}

// As the .dta results print a number; null when it is not finite.
std::string json_num(double v) {
    return std::isfinite(v) ? fixed(v) : "null";
}

std::vector<RunResult> sorted_runs(std::vector<RunResult> runs) {
    for (RunResult& r : runs)
        std::sort(
            r.objectives.begin(), r.objectives.end(),
            [](const ObjectiveResult& a, const ObjectiveResult& b) { return a.name < b.name; });
    std::sort(runs.begin(), runs.end(), [](const RunResult& a, const RunResult& b) {
        return a.scenario != b.scenario ? a.scenario < b.scenario : a.seed < b.seed;
    });
    return runs;
}

struct Tally {
    int passed = 0, failed = 0;
    std::vector<double> values;  // sorted by finish()
    void finish() { std::sort(values.begin(), values.end()); }
    [[nodiscard]] double median() const { return values[values.size() / 2]; }
    [[nodiscard]] std::string json() const {
        std::string s =
            "{\"passed\": " + std::to_string(passed) + ", \"failed\": " + std::to_string(failed);
        if (values.empty()) return s + ", \"min\": null, \"median\": null, \"max\": null}";
        return s + ", \"min\": " + json_num(values.front()) +
               ", \"median\": " + json_num(median()) + ", \"max\": " + json_num(values.back()) +
               "}";
    }
};

// Per objective, in name order, as summary_dta tallies them.
std::map<std::string, Tally> tallies(const std::vector<RunResult>& runs) {
    std::map<std::string, Tally> by;
    for (const RunResult& r : runs)
        for (const ObjectiveResult& o : r.objectives) {
            Tally& t = by[o.name];
            (o.pass ? t.passed : t.failed) += 1;
            if (o.error.empty() && std::isfinite(o.value)) t.values.push_back(o.value);
        }
    for (auto& [name, t] : by) t.finish();
    return by;
}

}  // namespace

std::string results_json(std::vector<RunResult> runs) {
    runs = sorted_runs(std::move(runs));
    const bool all =
        std::all_of(runs.begin(), runs.end(), [](const RunResult& r) { return r.passed(); });
    std::string s = "{\n  \"schema\": \"stacks.sim/1\",\n  \"status\": ";
    s += all ? "\"passed\"" : "\"failed\"";
    s += ",\n  \"runs\": [";
    for (size_t i = 0; i < runs.size(); ++i) {
        const RunResult& r = runs[i];
        s += i ? ",\n    {\"scenario\": " : "\n    {\"scenario\": ";
        s += json_str(r.scenario) + ", \"seed\": " + std::to_string(r.seed) +
             ", \"steps\": " + std::to_string(r.steps) +
             ", \"passed\": " + (r.passed() ? "true" : "false") + ", \"errors\": [";
        for (size_t e = 0; e < r.errors.size(); ++e) s += (e ? ", " : "") + json_str(r.errors[e]);
        s += "], \"objectives\": [";
        for (size_t j = 0; j < r.objectives.size(); ++j) {
            const ObjectiveResult& o = r.objectives[j];
            s += (j ? ", {\"name\": " : "{\"name\": ") + json_str(o.name) +
                 ", \"value\": " + json_num(o.value) +
                 ", \"pass\": " + (o.pass ? "true" : "false") + ", \"error\": " +
                 json_str(!o.error.empty() || std::isfinite(o.value) ? o.error : "not finite") +
                 "}";
        }
        s += "]}";
    }
    s += runs.empty() ? "],\n" : "\n  ],\n";
    s += "  \"summary\": [";
    const auto by = tallies(runs);
    size_t k = 0;
    for (const auto& [name, t] : by) {
        s += (k++ ? ",\n    {\"objective\": " : "\n    {\"objective\": ") + json_str(name) +
             ", \"tally\": " + t.json() + "}";
    }
    s += by.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}

bool read_runs(const fs::path& dir, std::vector<RunResult>& out, std::string& error) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        error = "no results directory: " + dir.string();
        return false;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("run-", 0) == 0 && entry.path().extension() == ".dta")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        error = "no run-*.dta files in " + dir.string() + ": run sim with --out there";
        return false;
    }
    auto number = [](const baker::Array& a) {
        if (a.size() < 2) return 0.0;
        if (auto i = a[1].try_int()) return static_cast<double>(*i);
        return a[1].try_float().value_or(0.0);
    };
    for (const fs::path& f : files) {
        auto tree = baker::read_file(f.string());
        if (!tree) {
            error = described(tree.error());
            return false;
        }
        const baker::Array* run = (*tree)->size() ? (**tree)[0].try_array() : nullptr;
        if (!run || run->empty() || !(*run)[0].is("run")) {
            error = f.string() + " is not a (run ...)";
            return false;
        }
        RunResult r;
        for (size_t i = 1; i < run->size(); ++i) {
            const baker::Array* field = (*run)[i].try_array();
            if (!field || field->size() < 2) continue;
            const baker::Node& tag = (*field)[0];
            if (tag.is("scenario")) {
                if (auto sym = (*field)[1].try_sym()) r.scenario = std::string(sym->str());
            } else if (tag.is("seed")) {
                r.seed = static_cast<uint64_t>(number(*field));
            } else if (tag.is("steps")) {
                r.steps = static_cast<uint64_t>(number(*field));
            } else if (tag.is("error")) {
                if (auto text = (*field)[1].try_str()) r.errors.emplace_back(*text);
            } else if (tag.is("objective")) {
                ObjectiveResult o;
                if (auto sym = (*field)[1].try_sym()) o.name = std::string(sym->str());
                for (size_t j = 2; j < field->size(); ++j) {
                    const baker::Array* part = (*field)[j].try_array();
                    if (!part || part->size() < 2) continue;
                    if ((*part)[0].is("value")) o.value = number(*part);
                    if ((*part)[0].is("pass")) o.pass = number(*part) != 0;
                    if ((*part)[0].is("error"))
                        if (auto text = (*part)[1].try_str()) o.error = std::string(*text);
                }
                r.objectives.push_back(std::move(o));
            }
        }
        out.push_back(std::move(r));
    }
    return true;
}

std::string compare_json(const std::vector<RunResult>& before, const std::vector<RunResult>& after,
                         bool& broke) {
    broke = false;
    // Pass by objective, then by (scenario, seed).
    using Key = std::pair<std::string, uint64_t>;
    std::map<std::string, std::map<Key, bool>> pass_before, pass_after;
    for (const RunResult& r : before)
        for (const ObjectiveResult& o : r.objectives)
            pass_before[o.name][{r.scenario, r.seed}] = o.pass;
    for (const RunResult& r : after)
        for (const ObjectiveResult& o : r.objectives)
            pass_after[o.name][{r.scenario, r.seed}] = o.pass;
    const auto tb = tallies(before), ta = tallies(after);
    std::map<std::string, int> names;
    for (const auto& [n, t] : tb) names[n] = 0;
    for (const auto& [n, t] : ta) names[n] = 0;

    std::string objectives;
    int broken = 0, fixed_count = 0;
    size_t k = 0;
    for (const auto& [name, unused] : names) {
        const auto b = tb.find(name), a = ta.find(name);
        std::string moved, changed;
        if (b == tb.end()) {
            moved = "new";
        } else if (a == ta.end()) {
            moved = "gone";
        } else {
            bool any_broke = false, any_fixed = false;
            size_t c = 0;
            for (const auto& [key, now] : pass_after[name]) {
                const auto was = pass_before[name].find(key);
                if (was == pass_before[name].end() || was->second == now) continue;
                (now ? any_fixed : any_broke) = true;
                changed += (c++ ? ", {\"scenario\": " : "{\"scenario\": ") + json_str(key.first) +
                           ", \"seed\": " + std::to_string(key.second) +
                           ", \"before\": " + (was->second ? "true" : "false") +
                           ", \"after\": " + (now ? "true" : "false") + "}";
            }
            moved = any_broke ? "broke" : any_fixed ? "fixed" : "same";
        }
        broken += moved == "broke" ? 1 : 0;
        fixed_count += moved == "fixed" ? 1 : 0;
        std::string change = "null";
        if (b != tb.end() && a != ta.end() && !b->second.values.empty() &&
            !a->second.values.empty())
            change = json_num(a->second.median() - b->second.median());
        objectives += (k++ ? ",\n    {\"name\": " : "\n    {\"name\": ") + json_str(name) +
                      ", \"moved\": " + json_str(moved) +
                      ", \"before\": " + (b != tb.end() ? b->second.json() : "null") +
                      ", \"after\": " + (a != ta.end() ? a->second.json() : "null") +
                      ", \"median_change\": " + change + ", \"changed\": [" + changed + "]}";
    }
    broke = broken > 0;
    return "{\n  \"schema\": \"stacks.compare/1\",\n  \"broke\": " + std::to_string(broken) +
           ",\n  \"fixed\": " + std::to_string(fixed_count) + ",\n  \"objectives\": [" +
           objectives + (names.empty() ? "]\n}\n" : "\n  ]\n}\n");
}

}  // namespace base
