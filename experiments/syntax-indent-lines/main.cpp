// The experiment's command line:
//   to-new <file.dta>       read DTA (the file alone: its includes as empty), print the lines form
//   to-dta <file>           read the lines form, print DTA in the house style (indent 3, width 100)
//   check                   round-trip every corpus file; exit 1 on an experiment fault
//   bench [<file.dta>...]   the translator against baker::read_string: on the generated text
//                           (4 MB), or on the files' trees in both spellings, repeated to 2 MB;
//                           then both readers on the same bracketed text (the translator's own
//                           overhead on a file that does not use the form)
//   gate [<file.dta>...]    bare atoms at a file's top level, and how many are first on their
//                           line, which the layout would make arrays of (the corpus by default)
//   count <file.dta>...     lines, bytes and parentheses of each file (alone) as written,
//                           reprinted bracketed in the house style, and as lines (Markdown)
//   convert [--write DIR] [<file.dta>...]
//                           the migration tool on each file (the corpus by default): its
//                           text with the parentheses of each array the layout reads back as
//                           that array deleted, comments and directives kept, read back
//                           with its includes and compared; lines, bytes and parentheses
//                           before and after (Markdown); --write saves each under DIR
//   to-lines-text <file>    the migration tool's text for one file
//   same <file.dta> <file>  whether a file in lines reads to the tree of one in today's text
//   trees [<file>...]       each file's tree as this build reads it (the corpus by default):
//                           a hash of its text, or its error; two builds' outputs, diffed,
//                           are the gate's exact answer (what a build that reads lines changes)
//
// In a build that reads lines (-DBAKER_INDENT_LINES=ALWAYS or DIRECTIVE) the reader and
// printer are the core's; else the translator's (syntax.hpp).
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace baker;
using namespace baker::experiments;

constexpr WriteOptions kHouse{.indent = 3, .width = 100};

int fail(const Error& e) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(describe(e).size()), describe(e).data());
    return 1;
}

int usage() {
    std::fprintf(
        stderr,
        "usage: baker_syntax_%.*s to-new <file.dta> | to-dta <file> | check | bench "
        "[<file.dta>...] | gate [<file.dta>...] | count <file.dta>... | trees "
        "[<file>...] | convert [--write DIR] [<file.dta>...] | to-lines-text <file> | same "
        "<file.dta> <file>\n",
        static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

size_t lines_of(std::string_view text) {
    size_t n = static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
    if (!text.empty() && text.back() != '\n') ++n;
    return n;
}
size_t parens_of(std::string_view text) {
    return static_cast<size_t>(std::count(text.begin(), text.end(), '(') +
                               std::count(text.begin(), text.end(), ')'));
}

// A file read alone: its own forms, with every file it includes or requires read as empty,
// so a data file's sample and count do not carry the shapes of the pack it requires.
Expected<Ref<Array>> read_alone(std::string_view path) {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    bool first = true;
    session.set_reader([first](std::string_view p, TextSink out) mutable {
        if (!first) return true;  // an included file: empty
        first = false;
        auto text = read_text_file(p);
        if (!text) return false;
        out(*text);
        return true;
    });
    return read_file(path, session);
}

// The files named, or the corpus, as paths from the repository root where relative.
std::vector<std::string> files_or_corpus(int argc, char** argv, int from) {
    std::vector<std::string> out;
    for (int i = from; i < argc; ++i) out.emplace_back(argv[i]);
    if (out.empty())
        for (const std::string& rel : corpus_files())
            out.push_back(std::string(repository_root()) + "/" + rel);
    return out;
}

std::string shown(const std::string& path) {
    const std::string root = std::string(repository_root()) + "/";
    return path.rfind(root, 0) == 0 ? path.substr(root.size()) : path;
}

// `text` repeated until it is at least `bytes` long.
std::string repeated(const std::string& text, size_t bytes) {
    std::string out;
    while (out.size() < bytes) out += text;
    return out;
}

int bench(int argc, char** argv) {
    std::string dta;
    if (argc > 2) {
        for (int i = 2; i < argc; ++i) {
            auto root = read_alone(argv[i]);
            if (!root) return fail(root.error());
            dta += to_dta(**root, kHouse);
        }
        dta = repeated(dta, 2 * 1024 * 1024);
        std::printf("the files' own trees, in both spellings, repeated to %zu bytes\n", dta.size());
    } else {
        dta = generated_config_text(4 * 1024 * 1024);
        std::printf("the parse benchmark's generated text\n");
    }
    auto root = read_string(dta, "bench.dta");
    if (!root) return fail(root.error());
    const std::string lines_text = syntax::print(**root);
    std::printf("\nbaker::read_string on brackets, the experiment on the same tree as lines:\n");
    compare_parsers(syntax::read, dta, lines_text);
#if BAKER_INDENT_LINES
    std::printf(
        "\nthe same bracketed text, read_string as built (layout %s) against the layout off:\n",
        BAKER_INDENT_LINES == 1 ? "on" : "off without #lines");
    compare_parsers(
        [](std::string_view text, std::string_view name) {
            LoadSession session;
            session.lines = false;
            return read_string(text, name, session);
        },
        dta, dta);
#else
    std::printf("\nboth readers on the same bracketed text (the translator's overhead):\n");
    compare_parsers(syntax::read, dta, dta);
#endif
    return 0;
}

int gate(int argc, char** argv) {
    size_t files = 0, scalars = 0, first = 0;
    for (const std::string& path : files_or_corpus(argc, argv, 2)) {
        auto text = read_text_file(path);
        if (!text) return fail(text.error());
        auto root = read_dta_file(path);
        if (!root) continue;  // a file Baker cannot read alone is not the gate's
        ++files;
        size_t here = 0;
        for (const Node& n : **root)
            if (!n.is_array_like()) ++here;
        const size_t lines = syntax::top_level_lines(*text);
        scalars += here;
        first += lines;
        if (here || lines)
            std::printf("%-50s %zu bare atoms at the top level, %zu first on their line\n",
                        shown(path).c_str(), here, lines);
    }
    std::printf(
        "%zu files: %zu bare atoms at the top level, %zu of them first on their line "
        "(each would become an array)\n",
        files, scalars, first);
    return 0;
}

// FNV-1a over the tree's compact text: equal trees, equal hashes.
uint64_t hash_of(std::string_view text) {
    uint64_t h = 14695981039346656037ull;
    for (const char c : text) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return h;
}

int trees(int argc, char** argv) {
    for (const std::string& path : files_or_corpus(argc, argv, 2)) {
        auto root = read_dta_file(path);
        if (root)
            std::printf("%s\tok %016llx\n", shown(path).c_str(),
                        static_cast<unsigned long long>(hash_of(to_string(Node(*root)))));
        else
            std::printf("%s\terror %s\n", shown(path).c_str(),
                        std::string(describe(root.error())).c_str());
    }
    return 0;
}

int convert(int argc, char** argv) {
    std::string dir;
    int from = 2;
    if (argc > 3 && std::string_view(argv[2]) == "--write") {
        dir = argv[3];
        from = 4;
    }
    struct Totals {
        size_t files = 0, lines[2] = {}, bytes[2] = {}, parens[2] = {}, arrays = 0, first = 0,
               made = 0, why[7] = {};
    } t;
    size_t failures = 0;
    std::printf(
        "| file | lines | bytes | parens | arrays: first on line / made lines | same tree |\n"
        "|---|---:|---:|---:|---:|---|\n");
    for (const std::string& path : files_or_corpus(argc, argv, from)) {
        auto text = read_text_file(path);
        if (!text) return fail(text.error());
        auto original = read_dta_file(path);
        if (!original) continue;  // a file Baker cannot read alone is not the tool's
        const syntax::Conversion c = syntax::to_lines_text(*text);
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        auto back = syntax::read_with(c.text, path, session);
        std::string same =
            back ? first_difference(**original, **back) : std::string(describe(back.error()));
        if (same.empty())
            same = "yes";
        else
            ++failures;
        std::printf("| %s | %zu -> %zu | %zu -> %zu | %zu -> %zu | %zu: %zu / %zu | %s |\n",
                    shown(path).c_str(), lines_of(*text), lines_of(c.text), text->size(),
                    c.text.size(), parens_of(*text), parens_of(c.text), c.arrays, c.first_on_line,
                    c.lines, same.c_str());
        ++t.files;
        t.lines[0] += lines_of(*text);
        t.lines[1] += lines_of(c.text);
        t.bytes[0] += text->size();
        t.bytes[1] += c.text.size();
        t.parens[0] += parens_of(*text);
        t.parens[1] += parens_of(c.text);
        t.arrays += c.arrays;
        t.first += c.first_on_line;
        t.made += c.lines;
        t.why[0] += c.untagged;
        t.why[1] += c.in_brackets;
        t.why[2] += c.under_values;
        t.why[3] += c.continued;
        t.why[4] += c.layout;
        t.why[5] += c.shares_line;
        t.why[6] += c.beside_tag;
        if (!dir.empty()) {
            std::string out = dir + "/" + shown(path);
            std::filesystem::create_directories(std::filesystem::path(out).parent_path());
            std::ofstream(out, std::ios::binary) << c.text;
        }
    }
    std::printf(
        "| **%zu files** | %zu -> %zu | %zu -> %zu | %zu -> %zu | %zu: %zu / %zu | %zu "
        "differ |\n",
        t.files, t.lines[0], t.lines[1], t.bytes[0], t.bytes[1], t.parens[0], t.parens[1], t.arrays,
        t.first, t.made, failures);
    std::printf(
        "\nkept in brackets, of %zu first on their line: %zu untagged or a macro, %zu in a "
        "bracketed parent, %zu under a parent holding a value, %zu with a line inside that "
        "begins with a value, %zu off the layout's indent (or a tab), %zu sharing their last "
        "line, %zu in a row wrapped under the node after their parent's tag\n",
        t.first, t.why[0], t.why[1], t.why[2], t.why[3], t.why[4], t.why[5], t.why[6]);
    return failures ? 1 : 0;
}

int count(int argc, char** argv) {
    if (argc < 3) return usage();
    struct Row {
        std::string name;
        size_t lines[3], bytes[3], parens[3];
    };
    std::vector<Row> rows;
    Row total{"total", {}, {}, {}};
    for (int i = 2; i < argc; ++i) {
        auto text = read_text_file(argv[i]);
        if (!text) return fail(text.error());
        auto root = read_alone(argv[i]);
        if (!root) return fail(root.error());
        const std::string forms[3] = {*text, to_dta(**root, kHouse), syntax::print(**root)};
        Row r{shown(argv[i]), {}, {}, {}};
        for (int f = 0; f < 3; ++f) {
            r.lines[f] = lines_of(forms[f]);
            r.bytes[f] = forms[f].size();
            r.parens[f] = parens_of(forms[f]);
            total.lines[f] += r.lines[f];
            total.bytes[f] += r.bytes[f];
            total.parens[f] += r.parens[f];
        }
        rows.push_back(r);
    }
    rows.push_back(total);
    std::printf(
        "| file | as written: lines / bytes / parens | reprinted bracketed (3, 100) | as "
        "lines |\n|---|---|---|---|\n");
    for (const Row& r : rows)
        std::printf("| %s | %zu / %zu / %zu | %zu / %zu / %zu | %zu / %zu / %zu |\n",
                    r.name.c_str(), r.lines[0], r.bytes[0], r.parens[0], r.lines[1], r.bytes[1],
                    r.parens[1], r.lines[2], r.bytes[2], r.parens[2]);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        auto root = read_alone(argv[2]);
        if (!root) return fail(root.error());
        std::fputs(syntax::print(**root).c_str(), stdout);
        return 0;
    }
    if (cmd == "to-dta" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        auto root = syntax::read(*text, argv[2]);
        if (!root) return fail(root.error());
        std::fputs(to_dta(**root, kHouse).c_str(), stdout);
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
    if (cmd == "bench") return bench(argc, argv);
    if (cmd == "gate") return gate(argc, argv);
    if (cmd == "count") return count(argc, argv);
    if (cmd == "trees") return trees(argc, argv);
    if (cmd == "convert") return convert(argc, argv);
    if (cmd == "same" && argc > 3) {  // a file in today's text, and one in lines
        auto original = read_dta_file(argv[2]);
        if (!original) return fail(original.error());
        auto text = read_text_file(argv[3]);
        if (!text) return fail(text.error());
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        auto back = syntax::read_with(*text, argv[3], session);
        if (!back) return fail(back.error());
        const std::string diff = first_difference(**original, **back);
        std::printf("%s\n", diff.empty() ? "same tree" : diff.c_str());
        return diff.empty() ? 0 : 1;
    }
    if (cmd == "to-lines-text" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        std::fputs(syntax::to_lines_text(*text).text.c_str(), stdout);
        return 0;
    }
    return usage();
}
