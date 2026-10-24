// The experiment's command line:
//   to-new <file.dta>     rewrite the file's operator calls as infix, comments and layout kept
//   to-dta <file>         read either form, print today's prefix DTA
//   check                 the corpus round trip (every command that can be infix, printed
//                         infix and read back), the converter over every corpus file (its
//                         text reads back to its tree, marks included), and each rewrite
//                         that rests on the vocabulary evaluated against what it replaced;
//                         exit 1 on a fault
//   stats                 the converter's counts per file, and every changed command
//   bench [<file.dta>]    baker::read_string on the DTA against itself on the infix text
//   read <file> <n>       read a file n times (to count instructions under cachegrind)
//   bench --operators     the generated text with two operator calls a record, printed
//                         prefix against printed infix: the same tree, only the spelling differs
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/script.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace baker;
using namespace baker::experiments;

int fail(const Error& e) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(describe(e).size()), describe(e).data());
    return 1;
}

int usage() {
    std::fprintf(stderr,
                 "usage: baker_syntax_%.*s to-new <file.dta> | to-dta <file> | check | stats | "
                 "bench [<file.dta> | --operators]\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

// Reads converted text as its original file was read: the same pack levels, includes from
// the file's directory.
Expected<Ref<Array>> read_converted(const std::string& text, const std::string& path) {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    session.add_include_dir(std::filesystem::path(path).parent_path().string());
    return read_string(text, path, session);
}

int count_marks(const Node& n) {
    if (!n.is_array_like()) return 0;
    int c = n.type() == Type::Command && n.array()->written_infix() ? 1 : 0;
    for (const Node& k : *n.array()) c += count_marks(k);
    return c;
}

void collect(const Node& n, std::vector<Symbol>& vars, std::vector<double>& numbers) {
    if (n.type() == Type::Var && std::find(vars.begin(), vars.end(), n.var_name()) == vars.end())
        vars.push_back(n.var_name());
    if (const auto i = n.try_int()) numbers.push_back(static_cast<double>(*i));
    if (const auto f = n.try_float()) numbers.push_back(*f);
    if (n.is_array_like())
        for (const Node& k : *n.array()) collect(k, vars, numbers);
}

std::string show(const Expected<Node>& r) {
    return r ? to_string(*r) : "error: " + std::string(r.error().message());
}

// Evaluates `before` and `after` with each variable they read set to values around each
// number they hold. Returns the first disagreement, or empty; `evaluated` is false when
// neither can run here (they call host functions this Context lacks).
std::string agree(const syntax::Rewrite& w, bool& evaluated) {
    std::vector<Symbol> vars;
    std::vector<double> numbers{0, 1};
    collect(w.before, vars, numbers);
    std::vector<Node> values;
    for (const double x : numbers)
        for (const double d : {-1.0, -1e-9, 0.0, 1e-9, 1.0}) values.emplace_back(x + d);
    for (const double x : numbers)
        if (x == static_cast<double>(static_cast<int64_t>(x)))
            values.emplace_back(static_cast<int64_t>(x));
    evaluated = false;
    const std::vector<Symbol>& names = vars;
    if (names.size() > 2) return {};
    const size_t n2 = names.size() == 2 ? values.size() : 1;
    for (size_t i = 0; i < (names.empty() ? 1 : values.size()); ++i) {
        for (size_t j = 0; j < n2; ++j) {
            Expected<Node> r[2];
            Node stored[2];
            for (int side = 0; side < 2; ++side) {
                Context ctx;
                if (!names.empty()) ctx.var(names[0]) = values[i];
                if (names.size() == 2) ctx.var(names[1]) = values[j];
                r[side] = ctx.eval(side == 0 ? w.before : w.after);
                if (!names.empty()) stored[side] = ctx.var(names[0]).value();
            }
            if (!r[0] &&
                std::string(r[0].error().message()).find("is not a function") != std::string::npos)
                return {};  // a host function: not evaluable here
            evaluated = true;
            const bool same = r[0] && r[1] ? structurally_equal(*r[0], *r[1]) : !r[0] && !r[1];
            if (!same || !structurally_equal(stored[0], stored[1]))
                return to_string(w.before) + " gives " + show(r[0]) + ", " + to_string(w.after) +
                       " gives " + show(r[1]);
        }
    }
    return {};
}

int check() {
    int faults = 0;
    // 1. The corpus round trip: every command that can be infix printed so, read back.
    const auto failures = round_trip_corpus(syntax::print, syntax::read);
    print_failures(failures);
    const auto round_trip_faults = experiment_faults(failures);
    faults += static_cast<int>(round_trip_faults.size());
    std::printf("round trip: %zu corpus files, %zu failures, %zu the experiment's\n",
                corpus_files().size(), failures.size(), round_trip_faults.size());

    // 2. The converter: its text reads back to its tree, marks included; and nothing in
    // today's corpus already reads as infix (the rule changes no file that exists).
    syntax::Counts total;
    int files = 0, converted_files = 0, fired_in_original = 0;
    std::vector<syntax::Rewrite> rewrites;
    for (const std::string& rel : corpus_files()) {
        const std::string path = std::string(repository_root()) + "/" + rel;
        auto original = read_dta_file(path);
        if (!original) continue;  // Baker's own reader refuses it alone: not ours
        ++files;
        for (const Node& n : **original) fired_in_original += count_marks(n);
        auto c = syntax::to_new(path);
        if (!c) {
            std::printf("convert  %s: %s\n", rel.c_str(), std::string(c.error().message()).c_str());
            ++faults;
            continue;
        }
        auto back = read_converted(c->text, path);
        if (!back) {
            std::printf("reread   %s: %s\n", rel.c_str(),
                        std::string(describe(back.error())).c_str());
            ++faults;
            continue;
        }
        if (std::string d = first_difference(*c->tree, **back); !d.empty()) {
            std::printf("compare  %s: %s\n", rel.c_str(), d.c_str());
            ++faults;
            continue;
        }
        if (std::string d = syntax::first_mark_difference(*c->tree, **back); !d.empty()) {
            std::printf("marks    %s: %s\n", rel.c_str(), d.c_str());
            ++faults;
            continue;
        }
        if (c->counts.top_changed > 0) ++converted_files;
        total.add(c->counts);
        rewrites.insert(rewrites.end(), c->rewrites.begin(), c->rewrites.end());
    }
    std::printf(
        "converter: %d files read, %d changed; %d of %d top-level commands rewritten "
        "(chains %d, bands %d, turned comparisons %d, ternaries %d, assignments %d, nests "
        "merged %d of which reassociated %d; %d left prefix for a comment)\n",
        files, converted_files, total.top_changed, total.top_commands, total.chains, total.bands,
        total.ascending, total.ternaries, total.assignments, total.flattened, total.reassociated,
        total.kept_for_comment);
    const auto sizes = [](const char* what, const syntax::TextSize& b, const syntax::TextSize& a) {
        std::printf("%s: brackets %d -> %d, atoms %d -> %d, characters %d -> %d (compact)\n", what,
                    b.brackets, a.brackets, b.atoms, a.atoms, b.chars, a.chars);
    };
    sizes("changed commands", total.changed_before, total.changed_after);
    sizes("all top-level commands", total.all_before, total.all_after);
    std::printf("braces today's corpus reads as infix: %d\n", fired_in_original);
    if (fired_in_original != 0) ++faults;

    // 3. The rewrites that rest on the vocabulary, evaluated against what they replaced.
    int evaluated = 0, skipped = 0;
    for (const syntax::Rewrite& w : rewrites) {
        bool ran = false;
        if (std::string d = agree(w, ran); !d.empty()) {
            std::printf("meaning  %s\n", d.c_str());
            ++faults;
        }
        ran ? ++evaluated : ++skipped;
    }
    std::printf(
        "rewrites evaluated against the original: %d agree, %d call host functions "
        "(not evaluated)\n",
        evaluated, skipped);
    std::printf("%d faults\n", faults);
    return faults == 0 ? 0 : 1;
}

int stats() {
    for (const std::string& rel : corpus_files()) {
        const std::string path = std::string(repository_root()) + "/" + rel;
        auto c = syntax::to_new(path);
        if (!c || c->counts.top_changed == 0) continue;
        const syntax::Counts& k = c->counts;
        std::printf(
            "## %s: %d of %d top-level commands; brackets %d -> %d, atoms %d -> %d, "
            "chars %d -> %d\n",
            rel.c_str(), k.top_changed, k.top_commands, k.changed_before.brackets,
            k.changed_after.brackets, k.changed_before.atoms, k.changed_after.atoms,
            k.changed_before.chars, k.changed_after.chars);
        for (const syntax::Example& e : c->examples)
            std::printf("%d: %s\n  -> %s\n", e.line, e.before.c_str(), e.after.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        auto c = syntax::to_new(argv[2]);
        if (!c) return fail(c.error());
        std::fputs(c->text.c_str(), stdout);
        return 0;
    }
    if (cmd == "to-dta" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        auto root = syntax::read(*text, argv[2]);
        if (!root) return fail(root.error());
        std::fputs(syntax::print_prefix(**root).c_str(), stdout);
        return 0;
    }
    if (cmd == "read" && argc > 3) {  // read a file N times: for counting instructions
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        const long n = std::strtol(argv[3], nullptr, 10);
        for (long i = 0; i < n; ++i)
            if (!syntax::read(*text, argv[2])) return 1;
        return 0;
    }
    if (cmd == "check") return check();
    if (cmd == "stats") return stats();
    if (cmd == "bench") {
        std::string text, converted;
        if (argc > 2 && std::string_view(argv[2]).starts_with("--operators")) {
            // Each record of the parse benchmark's text gains a band and a sum of a product,
            // the corpus's two commonest shapes; the converted text is the same tree, infix.
            const std::string plain = generated_config_text(4 * 1024 * 1024);
            const std::string_view at = "   (on_hit";
            size_t i = 0;
            for (size_t j; (j = plain.find(at, i)) != std::string::npos; i = j + at.size()) {
                text.append(plain, i, j - i);
                text +=
                    "   (pass {and {>= $value 0.3} {<= $value 0.7}})\n"
                    "   (score {+ {* $hits 10} $bonus})\n";
                text += at;
            }
            text.append(plain, i, std::string::npos);
            auto root = read_string(text, "bench.dta");
            if (!root) return fail(root.error());
            // Both printed, so the two texts differ only in how the operator calls are spelled.
            text = syntax::print_prefix(**root);
            converted = syntax::print(**root);
            // --operators=prefix or =infix prints that text instead, for `read` to count.
            if (std::string_view(argv[2]) == "--operators=prefix")
                return std::fputs(text.c_str(), stdout) < 0;
            if (std::string_view(argv[2]) == "--operators=infix")
                return std::fputs(converted.c_str(), stdout) < 0;
        } else if (argc > 2) {
            auto t = read_text_file(argv[2]);
            if (!t) return fail(t.error());
            text = *t;
            auto c = syntax::to_new(argv[2]);
            if (!c) return fail(c.error());
            converted = c->text;
        } else {
            text = generated_config_text(4 * 1024 * 1024);
            auto root = read_string(text, "bench.dta");
            if (!root) return fail(root.error());
            converted = syntax::print(**root);
        }
        compare_parsers(syntax::read, text, converted);
        return 0;
    }
    return usage();
}
