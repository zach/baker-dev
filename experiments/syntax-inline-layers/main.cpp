// The experiment's command line:
//   to-new <file.dta>       #ifdef regions to layer blocks, on the text; the regions it
//                           leaves, and why, to stderr
//   to-dta <file.dta>       a file's blocks back to #ifdef regions (the tree, printed)
//   check [<file.dta>...]   the corpus round trip, then each corpus file with conditionals
//                           (and each file named) converted and merged under every define
//                           set against its original; exit 1 on an experiment fault
//   compare <orig> <new>    a hand-made conversion against its original, read at the
//                           original's path, under every define set
//   survey <dir>            every .dta file under <dir>: its conditional regions, by what
//                           the converter makes of them
//   bench [<file.dta>]      Baker's reader on the original against the converted text
//   bench-layers [k]        a Config pushing 4 MB as one layer, and as a file with k blocks
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace {

using namespace baker;
using namespace baker::experiments;
namespace fs = std::filesystem;

int fail(const Error& e) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(describe(e).size()), describe(e).data());
    return 1;
}

int usage() {
    std::fprintf(stderr,
                 "usage: baker_syntax_inline-layers to-new <file.dta> | to-dta <file.dta> | "
                 "check [<file.dta>...] | compare <orig.dta> <new.dta> | survey <dir> | "
                 "bench [<file.dta>] | bench-layers [k]\n");
    return 2;
}

void report_regions(const std::string& file, const syntax::Conversion& c) {
    for (const syntax::Region& r : c.regions)
        std::fprintf(stderr, "%s:%d: #%s %s%s: %.*s\n", file.c_str(), r.line,
                     r.negated ? "ifndef" : "ifdef", r.define.c_str(), r.has_else ? " #else" : "",
                     static_cast<int>(syntax::describe(r.kind).size()),
                     syntax::describe(r.kind).data());
}

// One file's conversion, checked under every define set. Returns false on a fault.
bool check_file(const std::string& path, const std::string& label) {
    auto text = read_text_file(path);
    if (!text) return fail(text.error()) == 0;
    const syntax::Conversion c = syntax::to_blocks(*text);
    if (c.blocks.empty()) return true;
    int sets = 0, order = 0;
    const auto diffs = syntax::check_conversion(path, *text, c.text, &sets, &order);
    size_t converted = 0;
    for (const syntax::Region& r : c.regions)
        converted += r.kind == syntax::Region::Kind::Converted;
    std::printf(
        "%s: %zu of %zu regions as %zu blocks; %d define sets, %d equal, %d equal up to "
        "key order, %zu differ\n",
        label.c_str(), converted, c.regions.size(), c.blocks.size(), sets,
        sets - order - static_cast<int>(diffs.size()), order, diffs.size());
    for (const std::string& d : diffs) std::printf("  %s\n", d.c_str());
    return diffs.empty();
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        const syntax::Conversion c = syntax::to_blocks(*text);
        report_regions(argv[2], c);
        std::fputs(c.text.c_str(), stdout);
        return 0;
    }
    if (cmd == "to-dta" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        auto root = read_string(*text, argv[2], session);
        if (!root) return fail(root.error());
        std::fputs(syntax::to_ifdefs(**root, session.blocks()).c_str(), stdout);
        return 0;
    }
    if (cmd == "check") {
        const auto failures = round_trip_corpus(syntax::print, syntax::read);
        print_failures(failures);
        const auto faults = experiment_faults(failures);
        std::printf("round trip: %zu corpus files, %zu failures, %zu the experiment's\n",
                    corpus_files().size(), failures.size(), faults.size());
        bool ok = faults.empty();
        std::vector<std::pair<std::string, std::string>> files;
        for (const std::string& rel : corpus_files())
            files.emplace_back(std::string(repository_root()) + "/" + rel, rel);
        for (int i = 2; i < argc; ++i) files.emplace_back(argv[i], argv[i]);
        for (const auto& [path, label] : files) ok = check_file(path, label) && ok;
        return ok ? 0 : 1;
    }
    if (cmd == "compare" && argc > 3) {
        auto a = read_text_file(argv[2]);
        auto b = read_text_file(argv[3]);
        if (!a || !b) return fail(a ? b.error() : a.error());
        int sets = 0, order = 0;
        const auto diffs = syntax::check_conversion(argv[2], *a, *b, &sets, &order);
        std::printf("%s: %d define sets, %d equal, %d equal up to key order, %zu differ\n", argv[3],
                    sets, sets - order - static_cast<int>(diffs.size()), order, diffs.size());
        for (const std::string& d : diffs) std::printf("  %s\n", d.c_str());
        return diffs.empty() ? 0 : 1;
    }
    if (cmd == "survey" && argc > 2) {
        // --check: each file with a converted region merged against its conversion too.
        const bool check = argc > 3 && std::string_view(argv[3]) == "--check";
        std::map<std::string, int> kinds, checked;
        int files = 0, with = 0, regions = 0, defines = 0, top = 0;
        std::map<std::string, int> names;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(argv[2], ec), end; it != end; it.increment(ec)) {
            if (ec || !it->is_regular_file() || it->path().extension() != ".dta") continue;
            auto text = read_text_file(it->path().string());
            if (!text) continue;
            ++files;
            const syntax::Conversion c = syntax::to_blocks(*text);
            if (!c.regions.empty()) ++with;
            for (const syntax::Region& r : c.regions) {
                ++regions;
                ++kinds[std::string(syntax::describe(r.kind))];
                if (names[r.define]++ == 0) ++defines;
            }
            top += static_cast<int>(c.top_level);
            if (!check || c.blocks.empty()) continue;
            int sets = 0, order = 0;
            const auto diffs =
                syntax::check_conversion(it->path().string(), *text, c.text, &sets, &order);
            bool unread = false, single = false;  // a set of one define, or none, differs
            for (const std::string& d : diffs) {
                unread = unread || d.find(": the ") != std::string::npos;
                single = single || d.substr(0, d.find('}')).find(' ') == std::string::npos;
            }
            ++checked[unread           ? "a set did not read (an include the dump lacks?)"
                      : single         ? "differ under a set of one define, or none"
                      : !diffs.empty() ? "differ only with two or more defines set together"
                      : order > 0      ? "equal up to key order"
                                       : "equal under every define set"];
        }
        std::printf("%d files, %d with conditionals, %d regions over %d defines\n", files, with,
                    regions, defines);
        for (const auto& [k, n] : kinds) std::printf("  %5d  %s\n", n, k.c_str());
        std::printf("  of the converted, %d at the top level\n", top);
        for (const auto& [k, n] : checked) std::printf("  files: %5d  %s\n", n, k.c_str());
        return 0;
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
        const syntax::Conversion c = syntax::to_blocks(text);
        if (!read_string(text, "bench.dta")) return 1;
        compare_parsers(syntax::read, text, c.text);
        return 0;
    }
    if (cmd == "bench-layers") {
        const int k = argc > 2 ? std::atoi(argv[2]) : 8;
        const std::string text = generated_config_text(4 * 1024 * 1024);
        auto root = read_string(text, "bench.dta");
        if (!root) return fail(root.error());
        // The same forms: all in the file, or the first half in the file and the rest in k
        // blocks of consecutive forms.
        const size_t n = (*root)->size(), half = n / 2;
        std::string flat, blocks;
        for (size_t i = 0; i < n; ++i) flat += to_string((**root)[i]) + "\n";
        for (size_t i = 0; i < half; ++i) blocks += to_string((**root)[i]) + "\n";
        for (int b = 0; b < k; ++b) {
            blocks += "(#layer b" + std::to_string(b) + "\n";
            for (size_t i = half + (n - half) * b / k; i < half + (n - half) * (b + 1) / k; ++i)
                blocks += "   " + to_string((**root)[i]) + "\n";
            blocks += ")\n";
        }
        double best[2] = {1e30, 1e30};
        for (int round = 0; round < 7; ++round)
            for (int which = 0; which < 2; ++which) {
                Config cfg({.edit_mode = false, .tweaks = nullptr});
                const auto t0 = std::chrono::steady_clock::now();
                if (!cfg.push_layer_text("bench.dta", which ? blocks : flat)) return 1;
                best[which] = std::min(best[which], ms_since(t0));
                if (which == 1 && cfg.layers().size() != static_cast<size_t>(k) + 2) return 1;
            }
        std::printf("push_layer_text, %zu top-level forms, best of 7:\n", n);
        std::printf("  one layer:          %8.2f ms\n", best[0]);
        std::printf("  file and %3d blocks: %7.2f ms  (%.3fx)\n", k, best[1], best[1] / best[0]);
        return 0;
    }
    return usage();
}
