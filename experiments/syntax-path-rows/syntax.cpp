#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/path.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {
namespace {

// --- The tree, as merge.cpp and config_write.cpp read it ---

using detail::tag_of;

const Symbol& remove_marker() {
    static const Symbol s("#remove");
    return s;
}
bool is_tagged(const Node& n) noexcept {
    return !tag_of(n).empty();
}
bool has_values(const Array& a) noexcept {
    for (size_t i = 1; i < a.size(); ++i)
        if (!is_tagged(a[i])) return true;
    return false;
}
bool is_removal(const Array& a) noexcept {
    return a.size() == 2 && a[1].is(remove_marker());
}
// A bare symbol of two or more bytes beginning with '/': a row's key where it heads a
// parenthesized array.
bool is_row_tag(std::string_view s) noexcept {
    return s.size() > 1 && s[0] == '/';
}
// The bytes that end an atom for the lexer (text.hpp's kAtomEnd).
bool ends_atom(unsigned char c) noexcept {
    return c <= 0x20 || std::strchr("(){}[]\";", static_cast<char>(c)) != nullptr;
}
bool all_digits(std::string_view s) noexcept {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// --- Errors, located at a row ---

// What the reader knows about the text: its lines, for the key's column and for telling
// a bare key from a 'quoted' symbol (the parser's own hook tests the token instead).
struct Text {
    std::string_view text;
    Symbol file;  // the text's own file; an included file's arrays are located elsewhere
    std::vector<uint32_t> starts;  // byte offset of each line, built on first use

    std::string_view line(const Array& a) {
        if (a.file() != file || a.line() < 1) return {};
        if (starts.empty()) {
            starts.push_back(0);
            for (size_t i = 0; i < text.size(); ++i)
                if (text[i] == '\n') starts.push_back(static_cast<uint32_t>(i + 1));
        }
        const auto n = static_cast<size_t>(a.line());
        if (n > starts.size()) return {};
        const size_t from = starts[n - 1];
        const size_t to = n < starts.size() ? starts[n] : text.size();
        return text.substr(from, to - from);
    }
    // Whether the array's tag was written 'quoted' on its line: then it is a symbol, as
    // today, never a row. An approximation of the parser's `t.quoted`, good for one row a
    // line.
    bool quoted_tag(const Array& a, std::string_view key) {
        const std::string_view l = line(a);
        return l.find("'" + std::string(key) + "'") != std::string_view::npos;
    }
    // 1-based, in code points from the line's start, as the parser counts; 0 when unknown.
    int32_t column(const Array& a, std::string_view key) {
        const std::string_view l = line(a);
        // A row's arrays carry the row's line and their own tags, so the row's key is found
        // as the '(/' on that line; a literal's tag is found as itself.
        size_t at = key[0] == '/' ? l.find(key) : l.find("(/");
        if (at != std::string_view::npos && key[0] != '/') ++at;
        if (at == std::string_view::npos) at = l.find(key);
        if (at == std::string_view::npos) return 0;
        int32_t column = 1;
        for (size_t i = 0; i < at; ++i)
            if ((static_cast<unsigned char>(l[i]) & 0xC0) != 0x80) ++column;
        return column;
    }
};

Unexpected fail(Text& t, const Array& at, std::string_view key, const std::string& message) {
    Error e = make_error(ErrorCode::Syntax, message, at.file(), at.line());
    if (const int32_t c = t.column(at, key)) e.set_column(c);
    return Unexpected(std::move(e));
}

// Why a key Path refuses is refused: its segments counted as Path counts them (D27).
std::string key_problem(std::string_view key) {
    size_t segments = 0;
    for (size_t i = 0; i < key.size();) {
        ++segments;
        if (key[i] == '\'') {
            const size_t close = key.find('\'', i + 1);
            if (close == std::string_view::npos) return "the key has an unclosed quote";
            i = close + 1;
        } else {
            const size_t j = key.find_first_of("./", i);
            if (j == i) return "the key has an empty segment";
            i = std::min(j, key.size());
        }
        if (i < key.size() && key[i] != '.' && key[i] != '/')
            return "a quoted segment is followed by '.' or '/'";
        if (i < key.size() && ++i == key.size()) return "the key ends with a separator";
    }
    if (segments > Path::kMaxSegments)
        return "the key has more than " + std::to_string(Path::kMaxSegments) + " segments";
    return "the key has an empty segment";
}

// --- The rewrite: desugar, then fold ---

// `(/a.b.c body)` as `(a (b (c body)))`: the innermost array is the row's own with its
// first node replaced, each other segment a one-child wrapper; all with the row's file and
// line, so provenance names the row.
Expected<Ref<Array>> desugar(Text& t, const Array& row) {
    const std::string_view key = row[0].try_sym()->str();
    const Path path(key.substr(1));
    if (!path.valid()) return fail(t, row, key, key_problem(key.substr(1)));
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i].is_index)
            return fail(t, row, key,
                        "a row names keys, and '" + std::to_string(path[i].index) +
                            "' is an element; an element is set with config.set or by writing "
                            "the whole list");
        if (path[i].name.empty()) return fail(t, row, key, "the key has an empty segment");
    }
    std::vector<Node> inner;
    inner.reserve(row.size());
    inner.emplace_back(path[path.size() - 1].name);
    inner.insert(inner.end(), row.begin() + 1, row.end());
    Ref<Array> cur = Array::make_from(inner, row.file(), row.line());
    for (size_t i = path.size() - 1; i-- > 0;) {
        Node pair[2] = {Node(path[i].name), Node(std::move(cur))};
        cur = Array::make_from(std::span<Node>(pair, 2), row.file(), row.line());
    }
    return cur;
}

// A parenthesized array below the top level whose tag is a bare '/'-led symbol: a row
// where no row may stand (the mirror of D143's rule for #remove).
Expected<void> refuse_nested_rows(Text& t, const Node& n) {
    const Array* a = n.array();
    if (!a) return {};
    for (size_t i = 0; i < a->size(); ++i) {
        const Array* c = (*a)[i].try_array();
        if (c && c->size() > 0) {
            const auto s = (*c)[0].try_sym();
            if (s && is_row_tag(s->str()) && !t.quoted_tag(*c, s->str()))
                return fail(t, *c, s->str(),
                            "a row is a top-level form; write the key nested, or quote '" +
                                std::string(s->str()) + "' for a symbol");
        }
        if (auto r = refuse_nested_rows(t, (*a)[i]); !r) return r;
    }
    return {};
}

void append_name(std::string& path, Symbol name) {
    if (!path.empty()) path += '/';
    path += name.str();
}

Expected<void> fold_children(Text& t, std::vector<Node>& nodes, size_t from, std::string& path);

// `b` joined into `a`, two tagged arrays with one tag: a's nodes, then b's after its tag,
// their children folded in turn. Both holding values is the leaf written twice.
Expected<Ref<Array>> join(Text& t, const Array& a, const Array& b, std::string& path) {
    const std::string_view key = b[0].try_sym()->str();
    if (has_values(a) && has_values(b)) return fail(t, b, key, "'" + path + "' is written twice");
    if (is_removal(a) || is_removal(b))
        return fail(t, b, key, "'" + path + "' is removed and also written");
    std::vector<Node> nodes(a.begin(), a.end());
    nodes.insert(nodes.end(), b.begin() + 1, b.end());
    if (auto r = fold_children(t, nodes, 1, path); !r) return Unexpected(r.error());
    return Array::make_from(nodes, a.file(), a.line());
}

// Joins each tagged array in nodes[from..] into the earlier one with its tag.
Expected<void> fold_children(Text& t, std::vector<Node>& nodes, size_t from, std::string& path) {
    for (size_t i = from; i < nodes.size(); ++i) {
        const Symbol tag = tag_of(nodes[i]);
        if (tag.empty()) continue;
        for (size_t j = from; j < i; ++j) {
            if (tag_of(nodes[j]) != tag) continue;
            const size_t mark = path.size();
            append_name(path, tag);
            auto joined = join(t, *nodes[j].try_array(), *nodes[i].try_array(), path);
            path.resize(mark);
            if (!joined) return Unexpected(joined.error());
            nodes[j] = Node(std::move(*joined));
            nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(i));
            --i;
            break;
        }
    }
    return {};
}

}  // namespace

Expected<Ref<Array>> rows_to_tree(Ref<Array> root, std::string_view text, std::string_view name) {
    Text t{text, root->file(), {}};
    (void)name;
    std::vector<Node> nodes(root->begin(), root->end());
    bool saw_row = false;
    for (Node& n : nodes) {
        const Array* a = n.try_array();
        const auto s = a && a->size() > 0 ? (*a)[0].try_sym() : std::nullopt;
        if (s && is_row_tag(s->str()) && !t.quoted_tag(*a, s->str())) {
            auto d = desugar(t, *a);
            if (!d) return Unexpected(d.error());
            n = Node(std::move(*d));
            saw_row = true;
        } else if (auto r = refuse_nested_rows(t, n); !r) {
            return Unexpected(r.error());
        }
    }
    if (!saw_row) return root;
    std::string path;
    if (auto r = fold_children(t, nodes, 0, path); !r) return Unexpected(r.error());
    return Array::make_from(nodes, root->file(), root->line());
}

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    auto root = read_string(text, name, session);
    if (!root) return root;
    return rows_to_tree(std::move(*root), text, name);
}

// --- The printer ---

std::string key_text(std::string_view name, bool first) {
    if (name.empty()) return {};
    for (const char c : name)
        if (ends_atom(static_cast<unsigned char>(c)) || c == '\'') return {};
    // Quoted where Path would read the bare text otherwise: a separator, an index, and a
    // first segment starting with '*', since "/*" opens a comment.
    const bool quote = name.find_first_of("./") != std::string_view::npos || all_digits(name) ||
                       (first && name[0] == '*');
    return quote ? "'" + std::string(name) + "'" : std::string(name);
}

namespace {

constexpr int kIndent = 3;
constexpr size_t kWidth = 96;

// A tagged array a tweak reads as metadata (D88): kept in its owner's row.
bool is_metadata(Symbol tag) noexcept {
    static const Symbol kinds[] = {"range", "step", "desc", "note", "kind", "group"};
    return std::find(std::begin(kinds), std::end(kinds), tag) != std::end(kinds);
}

char open_of(Type t) noexcept {
    return t == Type::Command ? '{' : t == Type::Property ? '[' : '(';
}
char close_of(Type t) noexcept {
    return t == Type::Command ? '}' : t == Type::Property ? ']' : ')';
}

// A node in DTA, as the core's printer writes it, plus: a bare '/'-led symbol heading a
// parenthesized array is quoted, so it reads back as the symbol and not as a row (the
// mirror of D143's #remove rule); `marker`: the slot where #remove reads back bare.
void put_flat(std::string& out, const Node& n, bool head = false, bool marker = false) {
    if (const auto s = n.try_sym()) {
        if (marker && *s == remove_marker()) {
            out += "#remove";
        } else if (head && is_row_tag(s->str())) {
            out += '\'';
            out += s->str();
            out += '\'';
        } else {
            out += to_string(n);
        }
        return;
    }
    if (!n.is_array_like()) {
        out += to_string(n);
        return;
    }
    const Array& a = *n.array();
    out += open_of(n.type());
    for (size_t i = 0; i < a.size(); ++i) {
        if (i) out += ' ';
        put_flat(out, a[i], n.type() == Type::Array && i == 0,
                 n.type() == Type::Array && a.size() == 2 && i == 1 && a[0].try_sym());
    }
    out += close_of(n.type());
}

// The pretty form: compact where it fits in the width, else one node a line.
void put_pretty(std::string& out, const Node& n, size_t indent, bool head = false,
                bool marker = false) {
    const size_t mark = out.size();
    put_flat(out, n, head, marker);
    if (!n.is_array_like() || out.size() - mark + indent <= kWidth) return;
    out.resize(mark);
    const Array& a = *n.array();
    out += open_of(n.type());
    for (size_t i = 0; i < a.size(); ++i) {
        if (i) {
            out += '\n';
            out.append(indent + kIndent, ' ');
        }
        put_pretty(out, a[i], indent + kIndent, n.type() == Type::Array && i == 0,
                   n.type() == Type::Array && a.size() == 2 && i == 1 && a[0].try_sym());
    }
    out += close_of(n.type());
}

// One row: `(/path body...)`, the body wrapped when the line would run past the width.
void put_row(std::string& out, const std::string& path, std::span<const Node> body, bool marker) {
    const size_t mark = out.size();
    out += '(';
    out += path;
    for (const Node& n : body) {
        out += ' ';
        put_flat(out, n, false, marker);
    }
    out += ')';
    if (out.size() - mark <= kWidth) {
        out += '\n';
        return;
    }
    out.resize(mark);
    out += '(';
    out += path;
    for (const Node& n : body) {
        out += '\n';
        out.append(kIndent, ' ');
        put_pretty(out, n, kIndent, false, marker);
    }
    out += ")\n";
}

struct Printer {
    std::string out;
    PrintStats& stats;

    // Whether `c`, a child of a row's array at `depth` segments, is written as rows of its
    // own: a tagged child with values, or a section, that is not metadata and whose name
    // can be a segment.
    static bool hoistable(const Node& c, size_t depth) {
        const Array* a = c.try_array();
        if (!a || a->tag().empty() || depth + 1 > Path::kMaxSegments) return false;
        if (is_metadata(a->tag()) || key_text(a->tag().str(), false).empty()) return false;
        return a->size() > 1;  // a bare tag such as (optional) stays in the body
    }

    // `a`, whose key is `path`: the body as one row and each hoistable child as rows of
    // its own, when the hoisted children end the array and no tag repeats among its
    // children (the fold would join them); else one row holding the whole body.
    void rows(const Array& a, const std::string& path, size_t depth) {
        size_t first_hoisted = a.size();
        bool clean = true;
        for (size_t i = 1; i < a.size(); ++i) {
            if (hoistable(a[i], depth)) {
                if (first_hoisted == a.size()) first_hoisted = i;
            } else if (first_hoisted != a.size()) {
                clean = false;  // a value or metadata after a hoisted child: order would change
            }
            const Symbol tag = tag_of(a[i]);
            for (size_t j = 1; j < i && clean && !tag.empty(); ++j)
                if (tag_of(a[j]) == tag) clean = false;
        }
        const bool marker = is_removal(a);
        if (!clean || first_hoisted == a.size()) {
            if (!clean) ++stats.verbatim_rows;
            ++stats.rows;
            put_row(out, path, a.nodes().subspan(1), marker);
            return;
        }
        if (first_hoisted > 1) {
            ++stats.rows;
            put_row(out, path, a.nodes().subspan(1, first_hoisted - 1), marker);
        }
        for (size_t i = first_hoisted; i < a.size(); ++i) {
            const Array& c = *a[i].try_array();
            rows(c, path + '.' + key_text(c.tag().str(), false), depth + 1);
        }
    }

    void nested(const Node& n) {
        ++stats.nested_forms;
        put_pretty(out, n, 0, n.type() == Type::Array);
        out += '\n';
    }

    void root(const Array& root) {
        // A top-level tag written twice is a list today (D31); rows would fold the two, so
        // such a file is written as DTA throughout.
        bool repeats = false;
        for (size_t i = 0; i < root.size() && !repeats; ++i) {
            const Symbol tag = tag_of(root[i]);
            for (size_t j = 0; j < i && !tag.empty(); ++j)
                if (tag_of(root[j]) == tag) repeats = true;
        }
        if (repeats) ++stats.repeated_top_tags;
        for (const Node& n : root) {
            const Array* a = n.try_array();
            const std::string key = a && !a->tag().empty() ? key_text(a->tag().str(), true) : "";
            if (repeats || key.empty())
                nested(n);
            else
                rows(*a, "/" + key, 1);
        }
        // A reader skips a byte order mark at the very start of a file.
        if (out.starts_with("\xEF\xBB\xBF")) out.insert(0, " ");
    }
};

}  // namespace

std::string print(const Array& root, PrintStats& stats) {
    Printer p{{}, stats};
    p.root(root);
    return std::move(p.out);
}

std::string print(const Array& root) {
    PrintStats stats;
    return print(root, stats);
}

}  // namespace baker::experiments::syntax
