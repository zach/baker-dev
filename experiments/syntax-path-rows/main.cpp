// The experiment's command line:
//   to-new <file.dta>      read DTA, print it as rows
//   to-dta <file>          read rows, print DTA
//   check                  round-trip every corpus file; exit 1 on an experiment fault
//   bench [<file.dta>]     the rows reader against baker::read_string, same content
//   bench --stacks [MB]    the same over the stacks' data, replicated to MB megabytes (1)
//   stats <file.dta>...    leaves, depths, parentheses, lines and bytes, nested and as rows
//   gate                   the sigil counters over the corpus files and the sources' literals
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace baker;
using namespace baker::experiments;
using baker::detail::tag_of;

int fail(const Error& e) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(describe(e).size()), describe(e).data());
    return 1;
}

int usage() {
    std::fprintf(stderr,
                 "usage: baker_syntax_%.*s to-new <file.dta> | to-dta <file> | check | bench "
                 "[<file.dta> | --stacks [MB]] | stats <file.dta>... | gate\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

size_t count_lines(std::string_view s) {
    size_t n = 0;
    for (const char c : s) n += c == '\n';
    return n;
}
size_t count_parens(std::string_view s) {
    size_t n = 0;
    for (const char c : s) n += c == '(' || c == ')';
    return n;
}

// The leaves of a tree (tagged arrays with values, as collect_paths sees them) and the sum
// of their depths.
void leaves(const Array& a, size_t depth, size_t& count, size_t& depth_sum) {
    for (size_t i = 0; i < a.size(); ++i) {
        const Array* c = a[i].try_array();
        if (!c || c->tag().empty()) continue;
        bool values = false;
        for (size_t k = 1; k < c->size(); ++k)
            if (tag_of((*c)[k]).empty()) values = true;
        if (values) {
            ++count;
            depth_sum += depth + 1;
        }
        leaves(*c, depth + 1, count, depth_sum);
    }
}

// The stacks' data, every file's tree, as one root with each copy's top-level tags
// suffixed, until the nested text is `bytes` long.
std::string stacks_text(size_t bytes, std::string& rows_text) {
    std::vector<Ref<Array>> trees;
    for (const std::string& rel : corpus_files()) {
        if (rel.rfind("stacks/", 0) != 0 || rel.find("/data/") == std::string::npos) continue;
        auto t = read_dta_file(std::string(repository_root()) + "/" + rel);
        if (t) trees.push_back(*t);
    }
    std::vector<Node> all;
    std::string nested;
    for (int copy = 0; nested.size() < bytes; ++copy) {
        for (const Ref<Array>& t : trees)
            for (const Node& n : *t) {
                const Array* a = n.try_array();
                if (!a || a->tag().empty()) continue;
                std::vector<Node> nodes(a->begin(), a->end());
                nodes[0] = Node(Symbol(std::string(a->tag().str()) + "_c" + std::to_string(copy)));
                all.emplace_back(Array::make_from(nodes, a->file(), a->line()));
            }
        nested = to_dta(*Array::make_from(all), {.indent = 3, .width = 96});
    }
    rows_text = syntax::print(*Array::make_from(all));
    return nested;
}

// --- The gate: what a '/' sigil, and a sigil-free dotted tag, would reinterpret ---

struct Gate {
    size_t slash_tags_top = 0;     // paren arrays at the top level whose tag begins with '/'
    size_t slash_tags_nested = 0;  // the same below the top level
    size_t lone_slash_tags = 0;    // the symbol '/' as a tag
    size_t dotted_tags = 0;        // paren tags holding '.' or '/' (F's sigil-free trigger)
    size_t slash_values = 0;       // '/'-led bare symbols anywhere else
    size_t brackets = 0;
    size_t tokens = 0;
};

void count(Gate& g, std::string_view text, std::vector<std::string>& examples,
           std::string_view where) {
    const TextStructure s = read_structure(text);
    g.tokens += s.tokens.size();
    for (const TextBracket& b : s.brackets) {
        ++g.brackets;
        if (b.open != '(' || b.tag < 0) continue;
        const TextToken& t = s.tokens[static_cast<size_t>(b.tag)];
        if (t.kind != TextTokenKind::Symbol) continue;  // quoted tags are never rows
        const std::string_view tag = text.substr(t.begin, t.end - t.begin);
        if (tag == "/") ++g.lone_slash_tags;
        if (tag.size() > 1 && tag[0] == '/') {
            ++(b.parent < 0 ? g.slash_tags_top : g.slash_tags_nested);
            examples.push_back(std::string(where) + ":" + std::to_string(t.line) + "  " +
                               std::string(tag));
        }
        if (tag.find_first_of("./") != std::string_view::npos && tag != "/") {
            ++g.dotted_tags;
            examples.push_back(std::string(where) + ":" + std::to_string(t.line) + "  " +
                               std::string(tag) + "  (dotted)");
        }
    }
    for (const TextToken& t : s.tokens)
        if (t.kind == TextTokenKind::Symbol && t.end - t.begin > 1 && text[t.begin] == '/')
            ++g.slash_values;
    // Values were counted with the tags above.
    g.slash_values -= std::min(g.slash_values, g.slash_tags_top + g.slash_tags_nested);
}

// The "..." literals of a source file, unescaped as C++ and Python read \n, \t, \" and \\.
std::vector<std::string> literals_of(std::string_view src) {
    std::vector<std::string> out;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] != '"') continue;
        std::string lit;
        size_t j = i + 1;
        for (; j < src.size() && src[j] != '"' && src[j] != '\n'; ++j) {
            if (src[j] == '\\' && j + 1 < src.size()) {
                const char e = src[++j];
                lit += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            } else {
                lit += src[j];
            }
        }
        if (j < src.size() && src[j] == '"' && lit.find('(') != std::string::npos)
            out.push_back(lit);
        i = j;
    }
    return out;
}

int gate() {
    Gate files, literals;
    std::vector<std::string> examples;
    size_t n_files = 0, n_literals = 0;
    for (const std::string& rel : corpus_files()) {
        auto text = read_text_file(std::string(repository_root()) + "/" + rel);
        if (!text) continue;
        ++n_files;
        count(files, *text, examples, rel);
    }
    // The read_string literals of the tests, examples and stacks: every "..." holding a '('
    // in their C++ and Python sources, approximately (a literal split over lines is missed).
    namespace fs = std::filesystem;
    for (std::string_view dir : {"tests", "examples", "stacks"}) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(fs::path(repository_root()) / dir, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            const std::string ext = it->path().extension().string();
            if (!it->is_regular_file() || (ext != ".cpp" && ext != ".hpp" && ext != ".py"))
                continue;
            auto src = read_text_file(it->path().string());
            if (!src) continue;
            const std::string rel = fs::relative(it->path(), repository_root()).generic_string();
            for (const std::string& lit : literals_of(*src)) {
                ++n_literals;
                count(literals, lit, examples, rel);
            }
        }
    }
    auto row = [](const char* name, const Gate& g) {
        std::printf("%-44s %8zu %8zu %8zu %8zu %8zu %10zu\n", name, g.slash_tags_top,
                    g.slash_tags_nested, g.lone_slash_tags, g.dotted_tags, g.slash_values,
                    g.brackets);
    };
    std::printf("%-44s %8s %8s %8s %8s %8s %10s\n", "", "/tag top", "/tag in", "'/' tag", "dotted",
                "/value", "brackets");
    row(("corpus .dta files (" + std::to_string(n_files) + ")").c_str(), files);
    row(("source literals holding '(' (" + std::to_string(n_literals) + ")").c_str(), literals);
    for (const std::string& e : examples) std::printf("  %s\n", e.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        auto root = read_dta_file(argv[2]);
        if (!root) return fail(root.error());
        std::fputs(syntax::print(**root).c_str(), stdout);
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
    if (cmd == "check") {
        syntax::PrintStats stats;
        const auto failures = round_trip_corpus(
            [&](const Array& root) { return syntax::print(root, stats); }, syntax::read);
        print_failures(failures);
        const auto faults = experiment_faults(failures);
        std::printf("%zu corpus files, %zu failures, %zu the experiment's\n", corpus_files().size(),
                    failures.size(), faults.size());
        std::printf(
            "printed: %zu rows (%zu with a nested body), %zu top-level forms as DTA, %zu "
            "files nested throughout for a repeated top-level tag\n",
            stats.rows, stats.verbatim_rows, stats.nested_forms, stats.repeated_top_tags);
        return faults.empty() ? 0 : 1;
    }
    if (cmd == "bench") {
        std::string text, converted;
        if (argc > 2 && std::string_view(argv[2]) == "--stacks") {
            const size_t mb = argc > 3 ? static_cast<size_t>(std::atoi(argv[3])) : 1;
            text = stacks_text(mb * 1024 * 1024, converted);
        } else {
            if (argc > 2) {
                auto t = read_text_file(argv[2]);
                if (!t) return fail(t.error());
                text = *t;
            } else {
                text = generated_config_text(4 * 1024 * 1024);
            }
            auto root = read_string(text, "bench.dta");
            if (!root) return fail(root.error());
            converted = syntax::print(**root);
        }
        std::printf(
            "nested: %zu bytes, %zu lines, %zu parentheses; rows: %zu bytes, %zu lines, "
            "%zu parentheses\n",
            text.size(), count_lines(text), count_parens(text), converted.size(),
            count_lines(converted), count_parens(converted));
        compare_parsers(syntax::read, text, converted);
        return 0;
    }
    if (cmd == "stats" && argc > 2) {
        size_t total_leaves = 0, total_depth = 0;
        for (int i = 2; i < argc; ++i) {
            auto text = read_text_file(argv[i]);
            auto root = read_dta_file(argv[i]);
            if (!text || !root) return fail(text ? root.error() : text.error());
            size_t n = 0, depth = 0;
            leaves(**root, 0, n, depth);
            total_leaves += n;
            total_depth += depth;
            const std::string rows = syntax::print(**root);
            std::printf(
                "%s: %zu leaves, mean depth %.2f; nested %zu lines %zu bytes %zu parens; "
                "rows %zu lines %zu bytes %zu parens\n",
                argv[i], n, n ? static_cast<double>(depth) / static_cast<double>(n) : 0.0,
                count_lines(*text), text->size(), count_parens(*text), count_lines(rows),
                rows.size(), count_parens(rows));
        }
        if (argc > 3)
            std::printf("all: %zu leaves, mean depth %.2f\n", total_leaves,
                        total_leaves
                            ? static_cast<double>(total_depth) / static_cast<double>(total_leaves)
                            : 0.0);
        return 0;
    }
    if (cmd == "gate") return gate();
    return usage();
}
