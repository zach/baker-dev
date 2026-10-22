// The experiment's command line:
//   to-new [--nested] <file.dta>   read DTA, print the column-table form (--nested: no headers)
//   to-dta <file>                  read the column-table form, print DTA
//   desugar <file>                 print the reader's rewrite of a file: its tables expanded
//   check                          round-trip every corpus file; exit 1 on an experiment fault
//   stats                          the heuristic over the corpus: tables, rows, cells, holes
//   bench [<file.dta>]             the reader against baker::read_string, same content
//   bench-table [<bytes>]          the same over generated keyed tables (default 4 MB)
//   count <what> <input> <n>       read `input` n times, for cachegrind: what is baker,
//                                  new, lex (read_structure) or rewrite (desugar only);
//                                  input is a file, `gen` (the parse benchmark's text) or
//                                  `gen-table` (generated keyed tables, converted for new)
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
                 "usage: baker_syntax_%.*s to-new [--nested] <file.dta> | to-dta <file> | "
                 "desugar <file> | check | stats | bench [<file.dta>] | bench-table [<bytes>] | "
                 "count <baker|new|lex|rewrite> <file|gen|gen-table> <n>\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

// Keyed tables in today's nested form, in the stacks' style, at least `bytes` long: a
// table of ten records every 400 bytes or so, with a hole in every other row.
std::string generated_table_text(size_t bytes) {
    std::string text;
    int id = 0;
    while (text.size() < bytes) {
        const std::string n = std::to_string(id++);
        text += "; table " + n + "\n(moves_" + n + "\n";
        for (int r = 0; r < 10; ++r) {
            const std::string k = std::to_string(r);
            text += "   (move_" + k + " (button light) (startup " + std::to_string(4 + r) +
                    ") (active 2) (recovery " + std::to_string(7 + r) + ") (damage " +
                    std::to_string(30 + 10 * r) + ")";
            if (r % 2) text += " (push 0.12)";
            text += " (hitbox 0.3 1.3 0.55 0.2))\n";
        }
        text.back() = ')';
        text += "\n";
    }
    return text;
}

double best_ms(const std::function<void()>& fn, int runs = 7) {
    double best = 1e9;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

// The lexer alone over both texts (read_structure, D220), the proxy for what a core
// implementation saves: the same arrays to make from fewer bytes and tokens.
void lex_only(std::string_view nested, std::string_view headed) {
    size_t nested_tokens = 0, headed_tokens = 0;
    const double a = best_ms([&] { nested_tokens = read_structure(nested).tokens.size(); });
    const double b = best_ms([&] { headed_tokens = read_structure(headed).tokens.size(); });
    const double r = best_ms([&] { (void)syntax::desugar(headed, "bench.new"); });
    std::printf("\nlexer alone (read_structure)   bytes     tokens    best ms\n");
    std::printf("nested                      %9zu  %9zu  %9.3f\n", nested.size(), nested_tokens, a);
    std::printf("headed                      %9zu  %9zu  %9.3f\n", headed.size(), headed_tokens, b);
    std::printf("the rewrite alone (desugar) %9zu             %9.3f\n", headed.size(), r);
}

std::string input_text(std::string_view input, bool& ok) {
    ok = true;
    if (input == "gen") return generated_config_text(4 * 1024 * 1024);
    if (input == "gen-table") return generated_table_text(4 * 1024 * 1024);
    auto t = read_text_file(input);
    if (!t) {
        fail(t.error());
        ok = false;
        return {};
    }
    return *t;
}

void print_stats(const char* name, const syntax::TableStats& s) {
    std::printf(
        "%-44s %5zu %5zu %4zu %6zu %5zu %5zu %6ld  (order %zu, coverage %zu, cell %zu, "
        "repeat %zu, width %zu)\n",
        name, s.candidates, s.tables, s.rows, s.cells, s.holes, s.columns, s.parens_removed,
        s.rejected_order, s.rejected_coverage, s.rejected_cell, s.rejected_repeat,
        s.rejected_width);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        syntax::PrintOptions o;
        const char* path = argv[2];
        if (std::string_view(argv[2]) == "--nested" && argc > 3) {
            o.tables = false;
            path = argv[3];
        }
        auto root = read_dta_file(path);
        if (!root) return fail(root.error());
        std::fputs(syntax::print(**root, o).c_str(), stdout);
        return 0;
    }
    if (cmd == "to-dta" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        auto root = syntax::read(*text, argv[2]);
        if (!root) return fail(root.error());
        std::fputs(to_dta(**root, {.indent = 3, .width = 96}).c_str(), stdout);
        return 0;
    }
    if (cmd == "desugar" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        auto out = syntax::desugar(*text, argv[2]);
        if (!out) return fail(out.error());
        std::fputs(out->c_str(), stdout);
        return 0;
    }
    if (cmd == "check") {
        const auto failures =
            round_trip_corpus([](const Array& r) { return syntax::print(r); }, syntax::reader());
        print_failures(failures);
        const auto faults = experiment_faults(failures);
        std::printf("%zu corpus files, %zu failures, %zu the experiment's\n", corpus_files().size(),
                    failures.size(), faults.size());
        return faults.empty() ? 0 : 1;
    }
    if (cmd == "stats") {
        syntax::TableStats total;
        std::printf("%-44s %5s %5s %4s %6s %5s %5s %6s\n", "file", "cands", "tabls", "rows",
                    "cells", "holes", "cols", "parens");
        for (const std::string& rel : corpus_files()) {
            auto root = read_dta_file(std::string(repository_root()) + "/" + rel);
            if (!root) continue;
            const syntax::TableStats s = syntax::table_stats(**root);
            if (s.candidates == 0) continue;
            print_stats(rel.c_str(), s);
            total.candidates += s.candidates;
            total.tables += s.tables;
            total.rejected_order += s.rejected_order;
            total.rejected_coverage += s.rejected_coverage;
            total.rejected_cell += s.rejected_cell;
            total.rejected_repeat += s.rejected_repeat;
            total.rejected_width += s.rejected_width;
            total.rows += s.rows;
            total.columns += s.columns;
            total.cells += s.cells;
            total.holes += s.holes;
            total.parens_removed += s.parens_removed;
            total.names_removed += s.names_removed;
        }
        print_stats("total", total);
        return 0;
    }
    if (cmd == "bench" || cmd == "bench-table") {
        std::string text;
        if (cmd == "bench-table") {
            text = generated_table_text(argc > 2 ? std::strtoull(argv[2], nullptr, 10)
                                                 : 4 * 1024 * 1024);
        } else if (argc > 2) {
            auto t = read_text_file(argv[2]);
            if (!t) return fail(t.error());
            text = *t;
        } else {
            text = generated_config_text(4 * 1024 * 1024);
        }
        auto root = read_string(text, "bench.dta");
        if (!root) return fail(root.error());
        const std::string converted = syntax::print(**root);
        const syntax::TableStats s = syntax::table_stats(**root);
        std::printf("tables %zu, rows %zu, cells %zu, holes %zu\n", s.tables, s.rows, s.cells,
                    s.holes);
        compare_parsers(syntax::reader(), text, converted);
        if (s.tables > 0) lex_only(text, converted);
        return 0;
    }
    if (cmd == "count" && argc > 4) {
        const std::string_view what = argv[2];
        bool ok = false;
        std::string text = input_text(argv[3], ok);
        if (!ok) return 1;
        const int n = std::atoi(argv[4]);
        if (what != "baker") {
            auto root = read_string(text, "count.dta");
            if (!root) return fail(root.error());
            text = syntax::print(**root);
        }
        size_t sink = 0;
        for (int i = 0; i < n; ++i) {
            if (what == "baker") {
                auto r = read_string(text, "count.dta");
                if (!r) return fail(r.error());
                sink += (*r)->size();
            } else if (what == "new") {
                auto r = syntax::read(text, "count.new");
                if (!r) return fail(r.error());
                sink += (*r)->size();
            } else if (what == "lex") {
                sink += read_structure(text).tokens.size();
            } else if (what == "rewrite") {
                auto r = syntax::desugar(text, "count.new");
                if (!r) return fail(r.error());
                sink += r->size();
            } else {
                return usage();
            }
        }
        std::printf("%zu bytes, %d reads, %zu\n", text.size(), n, sink);
        return 0;
    }
    return usage();
}
