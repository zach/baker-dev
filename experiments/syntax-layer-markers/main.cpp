// The experiment's command line:
//   to-new <layer.dta> [--base <base.dta>]   a layer that restates the base's rows, with the
//                                            verbs and pins against the base (no base: as read)
//   to-dta <layer.dta> [--base <base.dta>]   the verbs resolved against the base, pins left out
//   check                                    round-trip every corpus file; exit 1 on a fault
//   bench [<file.dta>]                       the reader against baker::read_string, same content
//   bench-pins [rows]                        an overlay of value rows read plain and pinned
//   lists                                    the stacks' lists: a package's layer text today
//                                            and with a verb, in bytes, and what each reads
//   parse-n <file> <n>                       read a file n times (for cachegrind)
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using namespace baker;
using namespace baker::experiments;

int fail(const Error& e) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(describe(e).size()), describe(e).data());
    return 1;
}

int usage() {
    std::fprintf(stderr,
                 "usage: baker_syntax_%.*s to-new <layer.dta> [--base <base.dta>] | to-dta "
                 "<layer.dta> [--base <base.dta>] | check | bench [<file.dta>] | bench-pins "
                 "[rows] | lists\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

const char* kind_name(syntax::ListKind k) {
    switch (k) {
        case syntax::ListKind::Values: return "values";
        case syntax::ListKind::Keyed: return "keyed";
        case syntax::ListKind::KeyedRepeats: return "keyed, a tag twice";
        case syntax::ListKind::OneValue: return "one value";
    }
    return "";
}

// The stacks' lists, one row each, then the totals.
int lists() {
    size_t n = 0, add_before = 0, add_after = 0, drop_before = 0, drop_after = 0;
    size_t faults = 0, kinds[4] = {};
    std::printf("%-58s %5s %-18s %10s %10s %6s %6s\n", "list (file:line path)", "elems", "kind",
                "add today", "add verb", "works", "verb");
    for (const std::string& file : corpus_files()) {
        if (file.rfind("stacks/", 0) != 0 || file.find("/data/") == std::string::npos) continue;
        auto root = read_dta_file(std::string(repository_root()) + "/" + file);
        if (!root) continue;
        const std::string path = std::string(repository_root()) + "/" + file;
        for (const syntax::ListRow& r : syntax::list_rows(**root)) {
            if (r.file != path) continue;  // an included file's list, counted in that file
            ++n;
            ++kinds[static_cast<int>(r.kind)];
            const std::string where = file + ":" + std::to_string(r.line) + " " + r.path;
            std::printf("%-58s %5zu %-18s %4zu/%-5zu %4zu/%-5zu %3s/%-3s %3s/%-3s\n", where.c_str(),
                        r.elements, kind_name(r.kind), r.add_before, r.drop_before, r.add_after,
                        r.drop_after, r.add_today_works ? "yes" : "no",
                        r.drop_today_works ? "yes" : "no", r.add_same ? "yes" : "no",
                        r.drop_same ? "yes" : "no");
            if (r.kind == syntax::ListKind::OneValue) continue;
            if (!r.add_same || !r.drop_same) ++faults;
            if (r.kind == syntax::ListKind::Keyed) continue;  // keyed children need no verb
            add_before += r.add_before;
            add_after += r.add_after;
            drop_before += r.drop_before;
            drop_after += r.drop_after;
        }
    }
    std::printf(
        "%zu lists: %zu of values, %zu keyed, %zu keyed with a tag twice, %zu one value.\n"
        "Over the values and repeated-tag lists, a package's text to add one element: %zu "
        "bytes restated, %zu with #append; to take the first out: %zu restated, %zu with "
        "#drop. %zu lists where a verb misreads.\n",
        n, kinds[0], kinds[1], kinds[2], kinds[3], add_before, add_after, drop_before, drop_after,
        faults);
    return faults == 0 ? 0 : 1;
}

int bench_pins(size_t rows) {
    std::string plain = "(tuning\n", pinned = "(tuning\n";
    for (size_t i = 0; i < rows; ++i) {
        const std::string key = "   (item_" + std::to_string(i) + " (price ";
        plain += key + std::to_string(100 + i % 900) + ".5))\n";
        pinned += key + std::to_string(100 + i % 900) + ".5 #was " + std::to_string(90 + i % 900) +
                  ".25))\n";
    }
    plain += ")\n";
    pinned += ")\n";
    const Reader baker_read = [](std::string_view t, std::string_view name) {
        return read_string(t, name);
    };
    for (int round = 0; round < 3; ++round) {
        const BenchRow a = bench_reader("plain", baker_read, plain, "plain.dta", 20);
        const BenchRow b = bench_reader("pinned", baker_read, pinned, "pinned.dta", 20);
        std::printf(
            "%zu rows: plain %zu bytes %.3f ms %.1f MB/s (%.1f ns/row); pinned %zu bytes "
            "%.3f ms %.1f MB/s (%.1f ns/row)\n",
            rows, a.bytes, a.best_ms, a.mb_per_s, a.best_ms * 1e6 / double(rows), b.bytes,
            b.best_ms, b.mb_per_s, b.best_ms * 1e6 / double(rows));
    }
    return 0;
}

// argv[i..]: a file, then an optional --base <file>.
bool layer_and_base(int argc, char** argv, Ref<Array>& layer, Ref<Array>& base, int& status) {
    auto l = read_dta_file(argv[2]);
    if (!l) {
        status = fail(l.error());
        return false;
    }
    layer = *l;
    if (argc > 4 && std::string_view(argv[3]) == "--base") {
        auto b = read_dta_file(argv[4]);
        if (!b) {
            status = fail(b.error());
            return false;
        }
        base = *b;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if ((cmd == "to-new" || cmd == "to-dta") && argc > 2) {
        Ref<Array> layer, base;
        int status = 0;
        if (!layer_and_base(argc, argv, layer, base, status)) return status;
        if (base)
            layer = cmd == "to-new" ? syntax::to_markers(*base, *layer)
                                    : syntax::resolve(*base, *layer);
        std::fputs(syntax::print(*layer).c_str(), stdout);
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
    if (cmd == "bench") {
        std::string text;
        if (argc > 2) {
            auto t = read_text_file(argv[2]);
            if (!t) return fail(t.error());
            text = *t;
        } else {
            text = generated_config_text(4 * 1024 * 1024);
        }
        auto root = read_string(text, "bench.dta");
        if (!root) return fail(root.error());
        const std::string converted = syntax::print(**root);
        compare_parsers(syntax::read, text, converted);
        return 0;
    }
    if (cmd == "bench-pins") return bench_pins(argc > 2 ? std::stoul(argv[2]) : 20000);
    if (cmd == "parse-n" && argc > 3) {  // for cachegrind: read a file N times
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        for (unsigned long i = 0, n = std::stoul(argv[3]); i < n; ++i)
            if (!read_string(*text, "n.dta")) return 1;
        return 0;
    }
    if (cmd == "lists") return lists();
    return usage();
}
