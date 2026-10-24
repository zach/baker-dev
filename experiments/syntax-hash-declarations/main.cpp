// The experiment's command line:
//   to-new <file.dta>     migrate DTA text to the new form, keeping comments (report on stderr)
//   to-dta <file>         read the new form, print DTA
//   translate <file>      the DTA text the reader hands Baker for a file in the new form
//   check                 round-trip every corpus file; exit 1 on an experiment fault
//   migrate-check         migrate every corpus file's text, read it back through the new
//                         reader, compare the trees, packs, origins and macros; exit 1 on a
//                         mismatch (the translator's: without the core switch)
//   core-record           each corpus file as this build reads it, as text: without the
//                         core switch (BAKER_HASH_DECLARATIONS) the file as it is, with it
//                         the file migrated; the tree less its (shapes ...) containers, its
//                         value digest (D222), and the session (packs, origins, macros,
//                         shape declarations, the shapes they make, warnings)
//   core-check <record>   with the core switch: core-record's, compared with a record a
//                         build without the switch wrote; exit 1 on a mismatch
//   bench [<file.dta>]    the experiment's reader against baker::read_string, same content:
//                         the parse benchmark's 4 MB text, or a file (its #requires resolved,
//                         the stacks' pack levels), with the translate pass timed alone
//   count baker|new|translate <n>   read the 1 MB text n times, for cachegrind
//   bench-stack <stack> [rounds]    a stack's Config (default.dta and two text layers):
//                         reload, toggle and push, with its pack's (shapes ...) in the tree
//                         as today, and parsed but never merged, as the adopted #shape is
//   count-stack <stack> tree|records reload|toggle|push <n>   one of those n times, for
//                         cachegrind
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <baker/config.hpp>
#if BAKER_SYNTAX_SHAPE
#include <baker/shape.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
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

// --- core-record and core-check: the core's reader against today's, across two builds ---

// Every node with its type, so two builds' trees compare as text whatever their printers.
void dump(std::string& out, const Node& n) {
    switch (n.type()) {
        case Type::Int: out += "i" + std::to_string(*n.try_int()); return;
        case Type::Float: {
            char buf[40];
            std::snprintf(buf, sizeof buf, "f%.17g", *n.try_float());
            out += buf;
            return;
        }
        case Type::Symbol: out += "s'" + std::string(n.try_sym()->str()) + "'"; return;
        case Type::String: out += "\"" + std::string(*n.try_str()) + "\""; return;
        case Type::Var: out += "$" + std::string(n.var_name().str()); return;
        default: break;
    }
    const Array* a = n.array();
    if (!a) {
        out += "?" + std::string(type_name(n.type()));
        return;
    }
    out += n.type() == Type::Command ? '{' : n.type() == Type::Property ? '[' : '(';
    for (uint32_t i = 0; i < a->size(); ++i) {
        if (i) out += ' ';
        dump(out, (*a)[i]);
    }
    out += n.type() == Type::Command ? '}' : n.type() == Type::Property ? ']' : ')';
}
std::string dumped(const Array& a) {
    std::string out;
    dump(out, Node(Ref<Array>(const_cast<Array*>(&a))));
    return out;
}

// A top-level (shapes (NAME ...) ...) today's reader keeps in the tree: what the migrator
// splits into #shape declarations.
bool shapes_container(const Node& n) {
    const Array* a = n.array();
    if (!a || a->size() < 2 || !(*a)[0].is(Symbol("shapes"))) return false;
    for (uint32_t i = 1; i < a->size(); ++i) {
        const Array* c = (*a)[i].array();
        if (!c || c->empty() || !(*c)[0].try_sym()) return false;
    }
    return true;
}

std::string record_of(const std::string& rel, const Array& tree, const LoadSession& s) {
    std::string out = "== " + rel + "\n";
    // The tree, less the containers (with the switch, a tree never holds one).
    std::vector<Node> data, decls;
    for (const Node& n : tree) {
        if (shapes_container(n))
            for (const Node& c : *n.array()) decls.push_back(c);
        else
            data.push_back(n);
    }
    const Ref<Array> kept = Array::make_from(data, tree.file(), tree.line());
    char hex[32];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(digest(*kept)));
    out += "digest " + std::string(hex) + "\ntree " + dumped(*kept) + "\n";
    out += session_record(s);
    for (const Definition& d : s.macro_definitions())
        out += "defined " + std::string(d.name.str()) + " " + std::string(d.file.str()) + ":" +
               std::to_string(d.line) + "\n";
#if BAKER_HASH_DECLARATIONS
    (void)decls;
    for (const ShapeDecl& d : s.shapes())
        out += "declared " + std::string(d.file.str()) + ":" + std::to_string(d.line) + " " +
               dumped(*d.form) + "\n";
#else
    for (const Node& d : decls)
        if (const Array* a = d.array())  // a container's children, its tag aside
            out += "declared " + std::string(a->file().str()) + ":" + std::to_string(a->line()) +
                   " " + dumped(*a) + "\n";
#endif
#if BAKER_SYNTAX_SHAPE
    // What the declarations make: each shape and field, and the library's own report.
    shape::Set set;
    shape::Report report;
#if BAKER_HASH_DECLARATIONS
    set.add(s, report);
#else
    set.add(tree, report, &s);
#endif
    for (const shape::Shape& sh : set.shapes()) {
        out += "shape " + std::string(sh.name.str()) + " " + std::string(sh.file.str()) + ":" +
               std::to_string(sh.line) + (sh.keyed ? " keyed" : "") + " at";
        for (const Symbol a : sh.at) out += " " + std::string(a.str());
        out += "\n";
        for (const shape::Field& f : sh.fields) {
            char range[96];
            std::snprintf(range, sizeof range, " %d %.17g %.17g %.17g %u %u", f.ranged ? 1 : 0,
                          f.min, f.max, f.step, f.min_count, f.max_count);
            out += "  field " + std::string(f.name.str()) + " " +
                   std::to_string(static_cast<int>(f.type)) + "/" +
                   std::to_string(static_cast<int>(f.element)) + "/" + std::string(f.shape.str()) +
                   range + (f.optional ? " optional" : "") + " " + std::string(f.kind.str()) +
                   " '" + std::string(f.desc) + "' " + std::string(f.file.str()) + ":" +
                   std::to_string(f.line) + "\n";
        }
    }
    for (const Error& e : report.errors) out += "shape-error " + std::string(describe(e)) + "\n";
    for (const Error& e : report.warnings)
        out += "shape-warning " + std::string(describe(e)) + "\n";
#endif
    for (const Error& w : s.warnings()) out += "warning " + std::string(describe(w)) + "\n";
    return out;
}

// Each corpus file this build reads: as it is without the switch, migrated with it.
std::map<std::string, std::string> core_records() {
    std::map<std::string, std::string> out;
    const std::string root(repository_root());
    for (const std::string& rel : corpus_files()) {
        LoadSession s;
        s.set_pack_levels({"engine", "game", "task", "live"});
#if BAKER_HASH_DECLARATIONS
        syntax::serve_migrated(s);
#endif
        auto tree = read_file(root + "/" + rel, s);
        out[rel] = tree ? record_of(rel, **tree, s)
                        : "== " + rel + "\nunread " + std::string(describe(tree.error())) + "\n";
    }
    return out;
}

int core_record() {
    for (const auto& [rel, record] : core_records()) std::fputs(record.c_str(), stdout);
    return 0;
}

// When two record lines differ only in the numbers after a ':' (file:line), by one shift,
// that shift (today's line less the core's); else 0.
int line_shift(const std::string& x, const std::string& y) {
    size_t i = 0, j = 0;
    int shift = 0;
    while (i < x.size() && j < y.size()) {
        if (x[i] == ':' && y[j] == ':' && i + 1 < x.size() && j + 1 < y.size() &&
            std::isdigit(static_cast<unsigned char>(x[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(y[j + 1]))) {
            size_t ei = i + 1, ej = j + 1;
            while (ei < x.size() && std::isdigit(static_cast<unsigned char>(x[ei]))) ++ei;
            while (ej < y.size() && std::isdigit(static_cast<unsigned char>(y[ej]))) ++ej;
            const int d =
                std::stoi(x.substr(i + 1, ei - i - 1)) - std::stoi(y.substr(j + 1, ej - j - 1));
            if (d != 0 && shift != 0 && d != shift) return 0;
            if (d != 0) shift = d;
            i = ei;
            j = ej;
            continue;
        }
        if (x[i] != y[j]) return 0;
        ++i;
        ++j;
    }
    return i == x.size() && j == y.size() ? shift : 0;
}

// Splits a record file back into its files' records.
std::map<std::string, std::string> split_records(const std::string& text) {
    std::map<std::string, std::string> out;
    std::string rel;
    size_t at = 0;
    while (at < text.size()) {
        const size_t nl = std::min(text.find('\n', at), text.size());
        const std::string line = text.substr(at, nl - at);
        if (line.rfind("== ", 0) == 0) rel = line.substr(3);
        out[rel] += line + "\n";
        at = nl + 1;
    }
    return out;
}

int core_check(const char* record_path) {
    std::ifstream in(record_path);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", record_path);
        return 2;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto today = split_records(text);
    const auto mine = core_records();
    int files = 0, unread = 0, trees = 0, digests = 0, sessions = 0, moved_files = 0;
    std::map<int, int> shifts;
    for (const auto& [rel, was] : today) {
        if (was.find("\nunread ") != std::string::npos) {
            ++unread;  // today's reader refuses it alone: nothing to compare
            continue;
        }
        ++files;
        const auto it = mine.find(rel);
        if (it == mine.end()) {
            std::printf("missing  %s\n", rel.c_str());
            ++sessions;
            continue;
        }
        // Line by line; a line of the tree, the digest or the session that differs counts once.
        auto lines_of = [](const std::string& r) {
            std::vector<std::string> v;
            for (size_t a = 0; a < r.size();) {
                const size_t nl = std::min(r.find('\n', a), r.size());
                v.push_back(r.substr(a, nl - a));
                a = nl + 1;
            }
            return v;
        };
        const auto a = lines_of(was), b = lines_of(it->second);
        bool tree = false, dig = false, session = false, moved = false;
        for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
            const std::string x = i < a.size() ? a[i] : "", y = i < b.size() ? b[i] : "";
            if (x == y) continue;
            // Only a line number differs: the migrator removes a (shapes line, so what
            // follows it is declared a line or more earlier. Tallied by the shift.
            if (const int shift = line_shift(x, y); shift != 0) {
                ++shifts[shift];
                moved = true;
                continue;
            }
            const bool t = x.rfind("tree ", 0) == 0, d = x.rfind("digest ", 0) == 0;
            (t ? tree : d ? dig : session) = true;
            std::printf("%-8s %s\n  today: %s\n  core:  %s\n",
                        t   ? "tree"
                        : d ? "digest"
                            : "session",
                        rel.c_str(), x.c_str(), y.c_str());
        }
        trees += tree;
        digests += dig;
        sessions += session;
        moved_files += moved;
    }
    std::printf(
        "%d corpus files compared (%d today's reader refuses alone): %d tree, %d digest, %d "
        "session mismatches\n",
        files, unread, trees, digests, sessions);
    for (const auto& [shift, n] : shifts)
        std::printf(
            "%d entries (declarations, fields, definitions, warnings) %d line%s later today, "
            "in %d files: the (shapes line the migrator removes\n",
            n, shift, shift == 1 ? "" : "s", moved_files);
    return trees + digests + sessions == 0 ? 0 : 1;
}

int migrate_check() {
    int mismatches = 0, digests = 0;
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
        // The value digest (D222) of the tree each reader made.
        if (digest(**original) != digest(**back)) {
            std::printf("digest   %s\n", rel.c_str());
            ++digests;
        }
        if (session_record(plain) != session_record(migrated)) {
            std::printf("session  %s:\n--- original\n%s--- migrated\n%s", rel.c_str(),
                        session_record(plain).c_str(), session_record(migrated).c_str());
            ++mismatches;
        }
        // The main file's own rewrites (the first report is its text), one line per file.
        if (!reports.empty() && reports[0].total() > 0)
            std::printf(
                "migrated %-52s %d pack, %d origin, %d (shapes ...) -> %d #shape, %d "
                "#define bodies\n",
                rel.c_str(), reports[0].packs, reports[0].origins, reports[0].shape_sets,
                reports[0].shapes, reports[0].define_bodies);
        if (!reports.empty()) {
            total.packs += reports[0].packs;
            total.origins += reports[0].origins;
            total.shape_sets += reports[0].shape_sets;
            total.shapes += reports[0].shapes;
            total.define_bodies += reports[0].define_bodies;
        }
    }
    std::printf(
        "%zu corpus files, %d mismatches, %d digest mismatches; migrated %d (pack ...) first "
        "forms, %d (origin ...) forms, %d (shapes ...) containers into %d #shape "
        "declarations, %d #define bodies\n",
        corpus_files().size(), mismatches, digests, total.packs, total.origins, total.shape_sets,
        total.shapes, total.define_bodies);
    return mismatches + digests == 0 ? 0 : 1;
}

// The span of the top-level form opened at `at` (a '('), skipping strings, quoted symbols
// and comments; npos if it never closes.
size_t form_end(const std::string& text, size_t at) {
    int depth = 0;
    for (size_t i = at; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"' || c == '\'') {
            i = text.find(c, i + 1);
            if (i == std::string::npos) return i;
        } else if (c == ';') {
            i = text.find('\n', i);
            if (i == std::string::npos) return i;
        } else if (c == '(' || c == '{' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == '}' || c == ']') && --depth == 0) {
            return i + 1;
        }
    }
    return std::string::npos;
}

// The pack's text with its top-level (shapes ...) parsed into a macro body that nothing
// expands: read as the adopted #shape is (an ordinary array, parsed), never in the tree.
std::string shapes_unmerged(const std::string& pack) {
    const size_t at = pack.find("\n(shapes");
    if (at == std::string::npos) return pack;
    const size_t end = form_end(pack, at + 1);
    if (end == std::string::npos) return pack;
    return pack.substr(0, at + 1) + "[hd_shape_records " + pack.substr(at + 1, end - at - 1) + "]" +
           pack.substr(end);
}

double us_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
}

// With `variant` (tree or records) and `op` (reload, toggle or push): that one, `rounds`
// times, untimed, for cachegrind.
int bench_stack(const std::string& stack, int rounds, std::string_view variant = {},
                std::string_view op = {}) {
    const std::string root(repository_root());
    const std::string dir = root + "/stacks/" + stack;
    const std::string pack_path = dir + "/packs/game/" + stack + "_core.dta";
    auto pack = read_text_file(pack_path);
    if (!pack) return fail(pack.error());
    struct Variant {
        const char* name;
        std::string pack;
        std::unique_ptr<Config> cfg;
        int edits = 0;
        size_t root_nodes = 0;
        double reload = 1e30, toggle = 1e30, push = 1e30;
    };
    Variant vs[2] = {{"(shapes ...) in the tree", *pack, nullptr},
                     {"parsed, never merged", shapes_unmerged(*pack), nullptr}};
    const std::string overlay = "(hd_bench_local (a 1) (b 2.5) (c \"three\"))\n";
    const std::string agent = "(hd_bench_agent (a 2))\n";
    const bool counting = !variant.empty();
    for (Variant& v : vs) {
        if (counting && (&v == &vs[0]) != (variant == "tree")) continue;
        v.cfg = std::make_unique<Config>(ConfigOptions{.edit_mode = true, .tweaks = nullptr});
        LoadSession& session = v.cfg->session();
        session.set_pack_levels({"engine", "game", "task"});
        session.add_include_dir(dir + "/data");
        Variant* self = &v;
        session.set_reader([self, pack_path](std::string_view path, TextSink out) {
            if (path == pack_path) {
                out(self->pack);
                return true;
            }
            auto text = read_text_file(path);
            if (!text) return false;
            // An edit a reload sees: default.dta gains a comment line per edit.
            if (path.ends_with("/data/default.dta"))
                for (int i = 0; i < self->edits % 2; ++i) *text += "; edited\n";
            out(*text);
            return true;
        });
        if (auto r = v.cfg->push_layer("default", dir + "/data/default.dta"); !r)
            return fail(r.error());
        if (auto r = v.cfg->push_layer_text("local", overlay); !r) return fail(r.error());
        v.root_nodes = v.cfg->root()->size();
    }
    if (counting) {
        Variant& v = vs[variant == "tree" ? 0 : 1];
        for (int i = 0; i < rounds; ++i) {
            ++v.edits;
            if (op == "reload") {
                if (auto r = v.cfg->reload(); !r || !*r) return 1;
            } else if (op == "toggle") {
                if (!v.cfg->toggle_layer("local")) return 1;
            } else {
                if (!v.cfg->push_layer_text("agent", agent)) return 1;
                if (!v.cfg->remove_layer("agent")) return 1;
            }
        }
        std::printf("%d %.*s\n", rounds, static_cast<int>(op.size()), op.data());
        return 0;
    }
    for (int round = 0; round < rounds; ++round)
        for (Variant& v : vs) {
            ++v.edits;
            auto t0 = std::chrono::steady_clock::now();
            if (auto r = v.cfg->reload(); !r || !*r) return 1;
            v.reload = std::min(v.reload, us_since(t0));
            t0 = std::chrono::steady_clock::now();
            if (!v.cfg->toggle_layer("local")) return 1;
            v.toggle = std::min(v.toggle, us_since(t0));
            if (!v.cfg->toggle_layer("local")) return 1;
            t0 = std::chrono::steady_clock::now();
            if (!v.cfg->push_layer_text("agent", agent)) return 1;
            v.push = std::min(v.push, us_since(t0));
            if (!v.cfg->remove_layer("agent")) return 1;
        }
    std::printf("%s: %s, %zu bytes (%zu as records)\n", stack.c_str(),
                pack_path.c_str() + root.size() + 1, vs[0].pack.size(), vs[1].pack.size());
    std::printf("%-28s %6s %12s %12s %12s\n", "the pack's shapes", "nodes", "reload us",
                "toggle us", "push us");
    for (const Variant& v : vs)
        std::printf("%-28s %6zu %12.1f %12.1f %12.1f\n", v.name, v.root_nodes, v.reload, v.toggle,
                    v.push);
    std::printf("ratio (records / tree) %20.3f %12.3f %12.3f\n", vs[1].reload / vs[0].reload,
                vs[1].toggle / vs[0].toggle, vs[1].push / vs[0].push);
    return 0;
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
    if (cmd == "core-record") return core_record();
    if (cmd == "core-check" && argc > 2) return core_check(argv[2]);
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
        // Included files from memory on both sides, as they are and migrated (the
        // experiment's reader translates each as it reads it).
        static std::map<std::string, std::string, std::less<>> originals, migrated;
        static const auto held = [](bool migrate, std::string_view path) -> const std::string* {
            auto& files = migrate ? migrated : originals;
            auto it = files.find(path);
            if (it == files.end()) {
                auto file = read_text_file(path);
                if (!file) return nullptr;
                it = files.emplace(std::string(path), migrate ? syntax::migrate(*file) : *file)
                         .first;
            }
            return &it->second;
        };
        const Reader baker_read = [](std::string_view t, std::string_view n) {
            LoadSession session;
            session.set_pack_levels({"engine", "game", "task", "live"});
            session.set_reader([](std::string_view path, TextSink out) {
                const std::string* file = held(false, path);
                if (file) out(*file);
                return file != nullptr;
            });
            return read_string(t, n, session);
        };
        const Reader new_read = [](std::string_view t, std::string_view n) {
            LoadSession session;
            session.set_pack_levels({"engine", "game", "task", "live"});
            session.set_reader([](std::string_view path, TextSink out) {
                const std::string* file = held(true, path);
                if (!file) return false;
                auto dta = syntax::translate(*file, path);
                if (dta) out(*dta);
                return bool(dta);
            });
            return syntax::read_with(t, n, session);
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
            const BenchRow rn = bench_reader(n.name, new_read, converted, name, 3);
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
    if (cmd == "bench-stack" && argc > 2)
        return bench_stack(argv[2], argc > 3 ? std::atoi(argv[3]) : 200);
    if (cmd == "count-stack" && argc > 5)
        return bench_stack(argv[2], std::atoi(argv[5]), argv[3], argv[4]);
    return usage();
}
