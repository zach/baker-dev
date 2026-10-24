// The experiment's command line:
//   to-new <file.dta>     migrate DTA text to the new form, keeping comments (report on stderr)
//   to-dta <file>         read the new form, print DTA
//   translate <file>      the DTA text the reader hands Baker for a file in the new form
//   check                 round-trip every corpus file; exit 1 on an experiment fault
//   migrate-check         migrate every corpus file's text, read it back through the new
//                         reader, compare the trees, packs, origins and macros; exit 1 on a
//                         mismatch
//   bench [<file.dta>]    the experiment's reader against baker::read_string, same content:
//                         the parse benchmark's 4 MB text, or a file (its #requires resolved,
//                         the stacks' pack levels), with the translate pass timed alone
//   count baker|new|translate <n>   read the 1 MB text n times, for cachegrind
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <cstdlib>
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
                 "usage: baker_syntax_%.*s to-new <file.dta> | to-dta <file> | translate <file> | "
                 "check | migrate-check | bench [<file.dta>]\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

// What a session recorded while reading: its packs, origins and macro definitions, one
// line each, so two sessions can be compared as text.
std::string session_record(const LoadSession& s) {
    std::string out;
    for (const Pack& p : s.packs()) {
        out += "pack " + std::string(p.name.str()) + " level " + std::string(p.level.str()) +
               " rank " + std::to_string(p.rank) + " version '" + std::string(p.version.str()) +
               "' stage " + std::string(p.stage.str()) + " requires";
        for (size_t i = 0; i < p.requires_packs.size(); ++i)
            out += " " + std::string(p.requires_packs[i].str()) + "@" +
                   std::string(p.requires_versions[i].str());
        out += " owners";
        for (const Symbol o : p.owners) out += " " + std::string(o.str());
        out += " for";
        for (const Symbol a : p.audience) out += " " + std::string(a.str());
        out += "\n";
    }
    for (const Origin& o : s.origins())
        out +=
            "origin '" + std::string(o.source.str()) + "' tool " + std::string(o.tool.str()) + "\n";
    for (const Definition& d : s.macro_definitions())
        out += std::string(d.flag ? "flag " : "macro ") + std::string(d.name.str()) + "\n";
    return out;
}

int migrate_check() {
    int mismatches = 0;
    syntax::Migration total;
    const std::string root(repository_root());
    for (const std::string& rel : corpus_files()) {
        const std::string path = root + "/" + rel;
        LoadSession plain;
        plain.set_pack_levels({"engine", "game", "task", "live"});
        auto original = read_file(path, plain);
        if (!original) continue;  // Baker refuses it alone: not the experiment's business
        LoadSession migrated;
        migrated.set_pack_levels({"engine", "game", "task", "live"});
        std::vector<syntax::Migration> reports;
        syntax::serve_migrated(migrated, &reports);
        auto back = read_file(path, migrated);
        if (!back) {
            std::printf("read     %s: %s\n", rel.c_str(), to_string(back.error()).data());
            ++mismatches;
            continue;
        }
        if (const std::string d = first_difference(**original, **back); !d.empty()) {
            std::printf("tree     %s: %s\n", rel.c_str(), d.c_str());
            ++mismatches;
        }
        if (session_record(plain) != session_record(migrated)) {
            std::printf("session  %s:\n--- original\n%s--- migrated\n%s", rel.c_str(),
                        session_record(plain).c_str(), session_record(migrated).c_str());
            ++mismatches;
        }
        // The main file's own rewrites (the first report is its text).
        if (!reports.empty()) {
            total.packs += reports[0].packs;
            total.origins += reports[0].origins;
            total.shape_sets += reports[0].shape_sets;
            total.shapes += reports[0].shapes;
            total.define_bodies += reports[0].define_bodies;
        }
    }
    std::printf(
        "%zu corpus files, %d mismatches; migrated %d (pack ...) first forms, %d "
        "(origin ...) forms, %d (shapes ...) containers into %d #shape declarations, "
        "%d #define bodies\n",
        corpus_files().size(), mismatches, total.packs, total.origins, total.shape_sets,
        total.shapes, total.define_bodies);
    return mismatches == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        syntax::Migration m;
        std::fputs(syntax::migrate(*text, &m).c_str(), stdout);
        std::fprintf(stderr,
                     "%s: %d pack, %d origin, %d (shapes ...) -> %d #shape, %d #define bodies\n",
                     argv[2], m.packs, m.origins, m.shape_sets, m.shapes, m.define_bodies);
        return 0;
    }
    if ((cmd == "to-dta" || cmd == "translate") && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        std::vector<std::string> notes;
        if (cmd == "translate") {
            auto dta = syntax::translate(*text, argv[2], &notes);
            if (!dta) return fail(dta.error());
            std::fputs(dta->c_str(), stdout);
        } else {
            LoadSession session = syntax::corpus_session();
            auto root = syntax::read_with(*text, argv[2], session, &notes);
            if (!root) return fail(root.error());
            std::fputs(to_dta(**root, {.indent = 3, .width = 96}).c_str(), stdout);
        }
        for (const std::string& n : notes) std::fprintf(stderr, "note: %s\n", n.c_str());
        return 0;
    }
    if (cmd == "check") {
        const auto failures = round_trip_corpus(syntax::print, syntax::read);
        print_failures(failures);
        const auto faults = experiment_faults(failures);
        std::printf("%zu corpus files, %zu failures, %zu the experiment's\n", corpus_files().size(),
                    failures.size(), faults.size());
        return faults.empty() ? 0 : 1;
    }
    if (cmd == "migrate-check") return migrate_check();
    if (cmd == "bench") {
        std::string text, converted, name = "bench.dta";
        if (argc > 2) {
            auto t = read_text_file(argv[2]);
            if (!t) return fail(t.error());
            text = *t;
            // The same files on both sides: the file as it is, and the file migrated, each
            // #require of it read from disk.
            converted = syntax::migrate(text);
            name = argv[2];
        } else {
            text = generated_config_text(4 * 1024 * 1024);
            auto root = read_string(text, name);
            if (!root) return fail(root.error());
            converted = syntax::print(**root);
        }
        const Reader baker_read = [](std::string_view t, std::string_view n) {
            LoadSession session;
            session.set_pack_levels({"engine", "game", "task", "live"});
            return read_string(t, n, session);
        };
        const Reader translate_only = [](std::string_view t,
                                         std::string_view n) -> Expected<Ref<Array>> {
            auto d = syntax::translate(t, n);
            if (!d) return Unexpected(d.error());
            return Array::make(0);
        };
        BenchRow b{"baker::read_string", text.size(), 1e9, 0},
            n{"experiment", converted.size(), 1e9, 0},
            tr{"translate only", converted.size(), 1e9, 0};
        for (int round = 0; round < 5; ++round) {
            const BenchRow rb = bench_reader(b.name, baker_read, text, name, 3);
            const BenchRow rn = bench_reader(n.name, syntax::read, converted, name, 3);
            const BenchRow rt = bench_reader(tr.name, translate_only, converted, name, 3);
            if (rb.mb_per_s == 0 || rn.mb_per_s == 0 || rt.mb_per_s == 0) return 1;
            if (rb.best_ms < b.best_ms) b = rb;
            if (rn.best_ms < n.best_ms) n = rn;
            if (rt.best_ms < tr.best_ms) tr = rt;
        }
        std::printf("%-20s %10s %10s %10s\n", "reader", "bytes", "best ms", "MB/s");
        for (const BenchRow* row : {&b, &n, &tr})
            std::printf("%-20s %10zu %10.3f %10.1f\n", row->name.c_str(), row->bytes, row->best_ms,
                        row->mb_per_s);
        std::printf("time ratio (experiment / baker, same content): %.3f\n", n.best_ms / b.best_ms);
        return 0;
    }
    if (cmd == "count" && argc > 3) {
        const std::string_view which = argv[2];
        const int n = std::atoi(argv[3]);
        const std::string text = generated_config_text(1024 * 1024);
        auto root = read_string(text, "count.dta");
        if (!root) return fail(root.error());
        const std::string converted = syntax::print(**root);
        for (int i = 0; i < n; ++i) {
            if (which == "baker") {
                if (!read_string(text, "count.dta")) return 1;
            } else if (which == "new") {
                if (!syntax::read(converted, "count.dta")) return 1;
            } else if (!syntax::translate(converted, "count.dta")) {
                return 1;
            }
        }
        std::printf("%d reads of %zu bytes\n", n,
                    which == "baker" ? text.size() : converted.size());
        return 0;
    }
    return usage();
}
