#include "syntax.hpp"

#include "experiments.hpp"

#include <baker/std_interop.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace baker::experiments::syntax {

namespace {

constexpr std::string_view kHeader = "#columns";
constexpr std::string_view kHole = "_";
constexpr std::string_view kRemove = "#remove";

const Symbol& hole_sym() {
    static const Symbol s(kHole);
    return s;
}
const Symbol& remove_sym() {
    static const Symbol s(kRemove);
    return s;
}

// Characters, not bytes: UTF-8 continuation bytes are not counted.
size_t cp_len(std::string_view s) {
    size_t n = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// 1-based, in characters from the line's start, as the parser locates.
int32_t column_in(std::string_view text, size_t at) {
    at = std::min(at, text.size());
    if (at == 0) return 1;
    size_t start = text.rfind('\n', at - 1);
    start = start == std::string_view::npos ? 0 : start + 1;
    return static_cast<int32_t>(cp_len(text.substr(start, at - start))) + 1;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// A field as a cell: its one atom, command or property, or its values in parentheses. The
// symbol _ as a value is quoted, since bare it would read as a hole, and so is a list's
// first value when it is the bare name of a column, since bare it would read as a field
// written where a cell goes.
template <class IsColumn>
std::string cell_text(const Array& f, const IsColumn& is_column) {
    if (f.size() == 2 && !f[1].is_array_like())
        return f[1].is(hole_sym()) ? std::string("'_'") : to_string(f[1]);
    if (f.size() == 2 && f[1].type() != Type::Array) return to_string(f[1]);
    std::string s = "(";
    for (uint32_t i = 1; i < f.size(); ++i) {
        if (i > 1) s += ' ';
        std::string v = to_string(f[i]);
        if (i == 1 && f[i].type() == Type::Symbol && v == f[i].try_sym()->str() &&
            is_column(*f[i].try_sym()))
            v = "'" + v + "'";
        s += v;
    }
    return s + ")";
}

// A copy with every array's children in tag order (untagged first, each kind stable), for
// comparing trees that differ only in the order of a record's fields.
Ref<Array> canonical(const Array& a) {
    std::vector<Node> nodes;
    for (const Node& n : a) {
        if (n.type() == Type::Array)
            nodes.emplace_back(canonical(*n.array()));
        else
            nodes.push_back(n);
    }
    if (!nodes.empty()) {
        auto key = [](const Node& n) {
            const Array* arr = n.try_array();
            return arr ? std::string(arr->tag().str()) : std::string();
        };
        std::stable_sort(nodes.begin() + 1, nodes.end(),
                         [&](const Node& x, const Node& y) { return key(x) < key(y); });
    }
    return Array::make_from(nodes, a.file(), a.line());
}

// ---------------------------------------------------------------------------
// The reader: a rewrite of every headed table into nested text, on the same lines.

// One change to the text: [begin, end) becomes `text`. A blanking edit keeps the span's
// newlines and turns every other byte into a space, so no line moves.
struct Edit {
    uint32_t begin = 0;
    uint32_t end = 0;
    std::string text;
    bool blank = false;
};

// A column as the header wrote it: `name` is the symbol's text (quotes stripped), `written`
// what the rewrite inserts, which is the token as written, or quoted when the text defines
// a macro of that name, since a header name is never expanded.
struct Column {
    std::string name;
    std::string written;
};

// The rewrite of one text, and what write-back needs to map a change back onto it.
struct Rewrite {
    std::string text;         // the nested text
    std::vector<Edit> edits;  // sorted by begin, in offsets of the text as written
    struct Header {
        int32_t table = -1;  // the bracket it heads
        uint32_t at = 0;     // where it is written
        std::vector<Column> columns;
    };
    struct Cell {
        uint32_t begin = 0, end = 0;  // the cell's token or brackets, as written
        bool hole = false;
    };
    struct Row {
        size_t header = 0;
        std::string key;
        std::vector<Cell> cells;  // one per column
    };
    std::vector<Header> headers;
    std::vector<Row> rows;
    std::unordered_map<int32_t, size_t> row_of;  // a row's bracket -> its index in rows
    TextStructure structure;
};

// A direct child of a bracket: a token, or a nested bracket (opening at `tok`).
struct Child {
    int32_t tok = 0;
    int32_t bracket = -1;
};

class Rewriter {
public:
    Rewriter(std::string_view text, std::string_view name, const ReadOptions& options)
        : text_(text), file_(name), options_(options) {}

    Expected<Rewrite> run() {
        out_.structure = read_structure(text_, file_);
        index_brackets();
        // Every bracket that directly holds a #columns is a table, in text order.
        std::vector<int32_t> tables;
        const auto& toks = out_.structure.tokens;
        for (int32_t i = 0; i < static_cast<int32_t>(toks.size()); ++i) {
            const TextToken& t = toks[i];
            if (t.kind != TextTokenKind::Directive || span(t) != kHeader) continue;
            const int32_t parent = parent_of_[i];
            if (parent < 0)
                return fail<Rewrite>(ErrorCode::Directive,
                                     "#columns needs an enclosing parenthesized array: it declares "
                                     "the columns of that array's rows",
                                     t.line, t.begin);
            if (out_.structure.brackets[parent].open != '(')
                return fail<Rewrite>(ErrorCode::Directive,
                                     "#columns is written inside a parenthesized array, not inside "
                                     "'" +
                                         std::string(1, out_.structure.brackets[parent].open) + "'",
                                     t.line, t.begin);
            if (std::find(tables.begin(), tables.end(), parent) == tables.end())
                tables.push_back(parent);
        }
        for (const int32_t b : tables)
            if (auto r = table(b); !r) return Unexpected(std::move(r).error());
        apply();
        return std::move(out_);
    }

private:
    [[nodiscard]] std::string_view span(const TextToken& t) const {
        return text_.substr(t.begin, t.end - t.begin);
    }
    [[nodiscard]] std::string_view span(const TextBracket& b) const {
        return text_.substr(b.begin, b.end - b.begin);
    }
    // A symbol token's text without its quotes.
    [[nodiscard]] std::string_view symbol_text(const TextToken& t) const {
        const std::string_view s = span(t);
        if (t.kind == TextTokenKind::QuotedSymbol && s.size() >= 2)
            return s.substr(1, s.size() - 2);
        return s;
    }
    [[nodiscard]] static bool is_symbol(const TextToken& t) {
        return t.kind == TextTokenKind::Symbol || t.kind == TextTokenKind::QuotedSymbol ||
               t.kind == TextTokenKind::Macro;
    }
    [[nodiscard]] bool is_hole(const TextToken& t) const {
        return t.kind == TextTokenKind::Symbol && span(t) == kHole;
    }
    [[nodiscard]] const TextToken& tok(int32_t i) const { return out_.structure.tokens[i]; }
    [[nodiscard]] const TextBracket& bracket(int32_t b) const { return out_.structure.brackets[b]; }
    // A short quotation of some text, for a message.
    [[nodiscard]] static std::string quote(std::string_view s) {
        std::string q(s.substr(0, 40));
        for (char& c : q)
            if (c == '\n' || c == '\t' || c == '\r') c = ' ';
        return s.size() > 40 ? q + "..." : q;
    }

    template <class T>
    [[nodiscard]] Expected<T> fail(ErrorCode code, const std::string& message, int32_t line,
                                   uint32_t at) const {
        Error e = make_error(code, message, file_, line);
        e.set_column(column_in(text_, at));
        return Unexpected(std::move(e));
    }

    // The k-th Open token is bracket k (read_structure pushes one per Open token and
    // leaves stray closes out), so one pass with a stack maps tokens and brackets.
    void index_brackets() {
        const auto& toks = out_.structure.tokens;
        const auto nb = out_.structure.brackets.size();
        open_tok_.assign(nb, 0);
        close_tok_.assign(nb, static_cast<int32_t>(toks.size()));
        bracket_of_.assign(toks.size(), -1);
        parent_of_.assign(toks.size(), -1);
        std::vector<int32_t> stack;
        int32_t next = 0;
        for (int32_t i = 0; i < static_cast<int32_t>(toks.size()); ++i) {
            parent_of_[i] = stack.empty() ? -1 : stack.back();
            if (toks[i].kind == TextTokenKind::Open && next < static_cast<int32_t>(nb)) {
                bracket_of_[i] = next;
                open_tok_[next] = i;
                stack.push_back(next++);
            } else if (toks[i].kind == TextTokenKind::Close && !stack.empty()) {
                close_tok_[stack.back()] = i;
                stack.pop_back();
            }
        }
    }

    // The direct children of bracket `b`, in order; a nested bracket is one child.
    [[nodiscard]] std::vector<Child> children(int32_t b) const {
        std::vector<Child> out;
        const int32_t end = close_tok_[b];
        for (int32_t i = open_tok_[b] + 1; i < end;) {
            if (tok(i).kind == TextTokenKind::Open && bracket_of_[i] >= 0) {
                const int32_t c = bracket_of_[i];
                out.push_back({i, c});
                i = close_tok_[c] + 1;
            } else {
                out.push_back({i, -1});
                ++i;
            }
        }
        return out;
    }

    [[nodiscard]] std::string bracket_name(int32_t b) const {
        const TextBracket& br = bracket(b);
        if (br.tag >= 0) return std::string(symbol_text(tok(br.tag)));
        return "the array on line " + std::to_string(br.line);
    }
    [[nodiscard]] std::string child_text(const Child& c) const {
        return quote(c.bracket >= 0 ? span(bracket(c.bracket)) : span(tok(c.tok)));
    }

    // How many children after a directive are its arguments, so a table passes them
    // through untouched: #ifdef NAME, #define NAME (body), #include path, #autorun {cmd}.
    [[nodiscard]] size_t directive_arguments(const std::vector<Child>& kids, size_t k) const {
        const std::string_view d = span(tok(kids[k].tok));
        size_t n = 0;
        if (d == "#ifdef" || d == "#ifndef" || d == "#undef" || d == "#include" ||
            d == "#include_opt" || d == "#require" || d == "#merge" || d == "#autorun" ||
            d == "#define")
            n = 1;
        if (d == "#define" && k + 2 < kids.size() && kids[k + 2].bracket >= 0 &&
            bracket(kids[k + 2].bracket).open == '(' &&
            bracket(kids[k + 2].bracket).line == tok(kids[k + 1].tok).line)
            n = 2;
        return std::min(n, kids.size() - k - 1);
    }

    Expected<void> header(const std::vector<Child>& kids, size_t k, int32_t table) {
        const TextToken& t = tok(kids[k].tok);
        const bool has_list = k + 1 < kids.size() && kids[k + 1].bracket >= 0 &&
                              bracket(kids[k + 1].bracket).open == '(' &&
                              bracket(kids[k + 1].bracket).line == t.line;
        if (!has_list)
            return fail<void>(ErrorCode::Directive,
                              "#columns takes a parenthesized list of column names on its line: "
                              "#columns (name ...)",
                              t.line, t.begin);
        const int32_t h = kids[k + 1].bracket;
        if (!bracket(h).closed)
            return fail<void>(ErrorCode::Syntax, "the #columns list is not closed", t.line,
                              t.begin);
        std::vector<Column> columns;
        for (const Child& c : children(h)) {
            const TextToken& nt = tok(c.tok);
            if (c.bracket >= 0 || !is_symbol(nt))
                return fail<void>(ErrorCode::Directive,
                                  "#columns names each column with a symbol, not " +
                                      std::string(c.bracket >= 0 ? "an array" : span(nt)),
                                  nt.line, nt.begin);
            if (is_hole(nt))
                return fail<void>(ErrorCode::Directive, "'_' is a hole, not a column name", nt.line,
                                  nt.begin);
            const std::string name(symbol_text(nt));
            for (const Column& existing : columns)
                if (existing.name == name)
                    return fail<void>(ErrorCode::Directive, "#columns declares " + name + " twice",
                                      nt.line, nt.begin);
            std::string written(span(nt));
            if (nt.kind == TextTokenKind::Macro) written = "'" + written + "'";
            columns.push_back({name, std::move(written)});
        }
        if (columns.empty())
            return fail<void>(ErrorCode::Directive, "#columns declares no columns", t.line,
                              t.begin);
        out_.headers.push_back({table, t.begin, std::move(columns)});
        edits_.push_back({t.begin, bracket(h).end, {}, true});
        return {};
    }

    Expected<void> table(int32_t b) {
        const std::vector<Child> kids = children(b);
        bool headed = false;
        for (size_t k = 0; k < kids.size();) {
            const Child& c = kids[k];
            const TextToken& t = tok(c.tok);
            if (c.bracket >= 0) {
                if (headed)
                    if (auto r = row(c.bracket, out_.headers.size() - 1, b); !r) return r;
                ++k;
                continue;
            }
            if (t.kind == TextTokenKind::Directive) {
                if (span(t) == kHeader) {
                    if (auto r = header(kids, k, b); !r) return r;
                    headed = true;
                    k += 2;
                    continue;
                }
                k += 1 + directive_arguments(kids, k);
                continue;
            }
            if (headed)
                return fail<void>(ErrorCode::Syntax,
                                  "under #columns every child of " + bracket_name(b) +
                                      " is a row, (key cells...), not " + std::string(span(t)),
                                  t.line, t.begin);
            ++k;
        }
        return {};
    }

    void insert(uint32_t at, std::string text) {
        edits_.push_back({at, at, std::move(text), false});
    }

    Expected<void> row(int32_t r, size_t header_index, int32_t table) {
        const std::vector<Column>& columns = out_.headers[header_index].columns;
        const TextBracket& rb = bracket(r);
        if (rb.open != '(')
            return fail<void>(ErrorCode::Syntax,
                              "under #columns every child of " + bracket_name(table) +
                                  " is a row, (key cells...), not a " +
                                  (rb.open == '{' ? "command" : "property"),
                              rb.line, rb.begin);
        const std::vector<Child> kids = children(r);
        if (kids.empty() || kids[0].bracket >= 0 || tok(kids[0].tok).kind == TextTokenKind::Macro ||
            !is_symbol(tok(kids[0].tok)))
            return fail<void>(ErrorCode::Syntax,
                              "a row under #columns starts with its key, a symbol" +
                                  std::string(kids.empty() ? ""
                                              : kids[0].bracket >= 0
                                                  ? ", not an array"
                                                  : ", not " + std::string(span(tok(kids[0].tok)))),
                              rb.line, rb.begin);
        const TextToken& key = tok(kids[0].tok);
        const std::string key_text(span(key));
        if (is_hole(key))
            return fail<void>(ErrorCode::Syntax, "a row's key cannot be '_'", key.line, key.begin);
        const size_t n = columns.size();
        const size_t cells = kids.size() - 1;
        // (key #remove), a layer's removal, is not a row of cells and passes through.
        if (cells == 1 && kids[1].bracket < 0 &&
            tok(kids[1].tok).kind == TextTokenKind::Directive && span(tok(kids[1].tok)) == kRemove)
            return {};
        if (cells < n) {
            std::string missing;
            for (size_t i = cells; i < n; ++i) {
                if (!missing.empty()) missing += ' ';
                missing += columns[i].name;
            }
            return fail<void>(ErrorCode::Syntax,
                              "row " + key_text + " has " + std::to_string(cells) + " cell" +
                                  (cells == 1 ? "" : "s") + ", #columns declares " +
                                  std::to_string(n) + " (missing " + missing + ")",
                              rb.line, rb.begin);
        }
        if (cells > n && !options_.trailing_children) {
            const Child& extra = kids[1 + n];
            return fail<void>(ErrorCode::Syntax,
                              "row " + key_text + " has " + std::to_string(cells) +
                                  " cells, #columns declares " + std::to_string(n) +
                                  " (the first extra is " + child_text(extra) +
                                  "): a row is its key and one cell per column",
                              rb.line, rb.begin);
        }
        auto is_column = [&](std::string_view name) {
            return std::any_of(columns.begin(), columns.end(),
                               [&](const Column& c) { return c.name == name; });
        };
        Rewrite::Row record{header_index, key_text, {}};
        for (size_t i = 0; i < n; ++i) {
            const Child& c = kids[1 + i];
            const Column& col = columns[i];
            if (c.bracket >= 0) {
                const TextBracket& cb = bracket(c.bracket);
                if (!cb.closed)
                    return fail<void>(
                        ErrorCode::Syntax,
                        "row " + key_text + ": the cell for " + col.name + " is not closed",
                        cb.line, cb.begin);
                if (cb.open == '(') {
                    // A list that begins with a column's bare name is a field written whole
                    // where a cell goes: a nested row pasted under the header, or a cell
                    // dropped beside one. Both read silently wrong, so neither reads.
                    const std::vector<Child> inner = children(c.bracket);
                    if (!inner.empty() && inner[0].bracket < 0 &&
                        tok(inner[0].tok).kind == TextTokenKind::Symbol &&
                        is_column(span(tok(inner[0].tok))))
                        return fail<void>(
                            ErrorCode::Syntax,
                            "row " + key_text + ": the cell for " + col.name + ", " +
                                quote(span(cb)) + ", begins with the column name " +
                                std::string(span(tok(inner[0].tok))) +
                                ": a cell is its values alone, and a list that begins with a "
                                "column's name quotes it ('" +
                                std::string(span(tok(inner[0].tok))) + "' ...)",
                            cb.line, cb.begin);
                    insert(cb.begin + 1, col.written + " ");
                } else {
                    insert(cb.begin, "(" + col.written + " ");
                    insert(cb.end, ")");
                }
                record.cells.push_back({cb.begin, cb.end, false});
                continue;
            }
            const TextToken& t = tok(c.tok);
            if (t.kind == TextTokenKind::Directive)
                return fail<void>(ErrorCode::Syntax,
                                  "row " + key_text + ": " + std::string(span(t)) +
                                      " cannot be a cell (the cell for " + col.name + ")",
                                  t.line, t.begin);
            record.cells.push_back({t.begin, t.end, is_hole(t)});
            if (is_hole(t)) {
                edits_.push_back({t.begin, t.end, {}, true});
                continue;
            }
            insert(t.begin, "(" + col.written + " ");
            insert(t.end, ")");
        }
        for (size_t i = 1 + n; i < kids.size(); ++i) {
            const Child& c = kids[i];
            if (c.bracket < 0) {
                const TextToken& t = tok(c.tok);
                if (is_hole(t))
                    return fail<void>(ErrorCode::Syntax,
                                      "'_' after the " + std::to_string(n) + " cells of row " +
                                          key_text + ": #columns has no column for it",
                                      t.line, t.begin);
                continue;
            }
            const TextBracket& cb = bracket(c.bracket);
            if (cb.open != '(' || cb.tag < 0) continue;
            const std::string_view tag = symbol_text(tok(cb.tag));
            if (is_column(tag))
                return fail<void>(ErrorCode::Syntax,
                                  "row " + key_text + " writes " + std::string(tag) +
                                      " as a cell and as a child; write it once, as the cell",
                                  cb.line, cb.begin);
        }
        out_.row_of[r] = out_.rows.size();
        out_.rows.push_back(std::move(record));
        return {};
    }

    void apply() {
        std::stable_sort(edits_.begin(), edits_.end(),
                         [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
        std::string& out = out_.text;
        out.reserve(text_.size() + edits_.size() * 8);
        uint32_t pos = 0;
        std::vector<Edit> kept;  // overlapping edits cannot arise; one that did is left out
        for (Edit& e : edits_) {
            if (e.begin < pos) continue;
            out.append(text_, pos, e.begin - pos);
            if (e.blank) {
                for (uint32_t i = e.begin; i < e.end; ++i) out += text_[i] == '\n' ? '\n' : ' ';
            } else {
                out += e.text;
            }
            pos = e.end;
            kept.push_back(std::move(e));
        }
        out.append(text_, pos, std::string_view::npos);
        out_.edits = std::move(kept);
    }

    std::string_view text_;
    Symbol file_;
    ReadOptions options_;
    Rewrite out_;
    std::vector<int32_t> open_tok_, close_tok_;  // per bracket: its Open and Close tokens
    std::vector<int32_t> bracket_of_;            // per token: its bracket, for an Open
    std::vector<int32_t> parent_of_;             // per token: the bracket it is in
    std::vector<Edit> edits_;
};

Expected<Rewrite> rewrite(std::string_view text, std::string_view name,
                          const ReadOptions& options) {
    if (text.find(kHeader) == std::string_view::npos) {
        Rewrite same;
        same.text = text;
        return same;
    }
    return Rewriter(text, name, options).run();
}

// --- Offsets in the rewritten text, and where they are in the text as written ---

size_t written_length(const Edit& e) {
    return e.blank ? e.end - e.begin : e.text.size();
}

// Where offset `d` of the rewritten text is in the text as written. Inside or at either
// end of inserted text, it is where the text was inserted; `inside` says strictly inside.
size_t to_original(const std::vector<Edit>& edits, size_t d, bool* inside = nullptr) {
    long delta = 0;  // rewritten offset minus original offset, before the current edit
    if (inside) *inside = false;
    for (const Edit& e : edits) {
        const size_t db = e.begin + delta;
        if (d < db) break;
        const size_t len = written_length(e);
        if (d <= db + len && !e.blank) {
            if (inside) *inside = d > db && d < db + len;
            return e.begin;
        }
        if (d < db + len) return d - delta;  // a blank keeps its length
        delta += static_cast<long>(len) - static_cast<long>(e.end - e.begin);
    }
    return d - delta;
}

// Whether [a, b) of the rewritten text was copied from the text as written: no inserted
// text and no blank in it.
bool copied(const std::vector<Edit>& edits, size_t a, size_t b) {
    long delta = 0;
    for (const Edit& e : edits) {
        const size_t db = e.begin + delta;
        if (db >= b) break;
        const size_t len = written_length(e);
        if (len > 0 && db + len > a) return false;
        delta += static_cast<long>(len) - static_cast<long>(e.end - e.begin);
    }
    return true;
}

// The end of the blank at rewritten offset `d`, if `d` is in one.
std::optional<size_t> blank_end(const std::vector<Edit>& edits, size_t d) {
    long delta = 0;
    for (const Edit& e : edits) {
        const size_t db = e.begin + delta;
        if (d < db) break;
        const size_t len = written_length(e);
        if (e.blank && d < db + len) return db + len;
        delta += static_cast<long>(len) - static_cast<long>(e.end - e.begin);
    }
    return std::nullopt;
}

// A parser error located in the rewritten text, located in the text as written: the
// rewrite keeps every line, so only the column moves.
Error relocate(Error e, const Rewrite& rw, std::string_view original, std::string_view name) {
    if (rw.edits.empty() || e.file().str() != name || e.line() <= 0 || e.column() <= 0) return e;
    const std::string& text = rw.text;
    size_t d = 0;
    for (int32_t line = 1; line < e.line(); ++line) {
        d = text.find('\n', d);
        if (d == std::string::npos) return e;
        ++d;
    }
    for (int32_t c = 1; c < e.column() && d < text.size() && text[d] != '\n'; ++c) {
        ++d;
        while (d < text.size() && (static_cast<unsigned char>(text[d]) & 0xC0) == 0x80) ++d;
    }
    e.set_column(column_in(original, to_original(rw.edits, d)));
    return e;
}

Error refusal(std::string_view path, const std::string& message) {
    return make_error(ErrorCode::Refused, message, Symbol(path));
}

}  // namespace

// ---------------------------------------------------------------------------
// Headed files under a Config.

struct HeadedFiles::File {
    std::string original;
    Rewrite rewrite;
};

namespace {

bool disk_load(std::string_view path, std::string& out) {
    std::FILE* f = std::fopen(std::string(path).c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[1 << 14];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    const bool ok = std::ferror(f) == 0;  // a directory opens, and fails to read
    std::fclose(f);
    return ok;
}

bool disk_store(std::string_view path, std::string_view text) {
    std::FILE* f = std::fopen(std::string(path).c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

// One change to the text as written: [begin, end) becomes `text`.
struct Change {
    size_t begin = 0, end = 0;
    std::string text;
};

// The entry write_back_to inserted, read alone: one tagged array.
Expected<Ref<Array>> entry_of(std::string_view inserted, std::string_view path) {
    auto root = read_string(inserted, path);
    if (!root) return Unexpected(std::move(root).error());
    if ((*root)->size() != 1 || (**root)[0].type() != Type::Array ||
        (**root)[0].array()->tag().str().empty())
        return Unexpected(refusal(path, "cannot read what write-back inserts as one entry"));
    return Ref<Array>((**root)[0].mutable_array());
}

// What write_back_to's insertion at `at` (in the text as written) becomes when it lands in
// a headed row or between a headed table's rows.
Expected<Change> place(const Rewrite& rw, const ReadOptions& options, size_t at,
                       std::string_view inserted, std::string_view path) {
    // The innermost bracket around `at`.
    int32_t inner = -1;
    for (int32_t b = 0; b < static_cast<int32_t>(rw.structure.brackets.size()); ++b) {
        const TextBracket& br = rw.structure.brackets[b];
        if (br.begin < at && at < br.end) inner = b;
    }
    const Change plain{at, at, std::string(inserted)};
    if (inner < 0) return plain;
    const size_t lead = inserted.find_first_not_of(" \t\r\n");
    if (lead == std::string_view::npos) return plain;
    const std::string_view ws = inserted.substr(0, lead);

    if (auto r = rw.row_of.find(inner); r != rw.row_of.end()) {
        const Rewrite::Row& row = rw.rows[r->second];
        const std::vector<Column>& columns = rw.headers[row.header].columns;
        auto entry = entry_of(inserted, path);
        if (!entry) return Unexpected(std::move(entry).error());
        const std::string tag((*entry)->tag().str());
        auto is_column = [&](Symbol s) {
            return std::any_of(columns.begin(), columns.end(),
                               [&](const Column& c) { return c.name == s.str(); });
        };
        for (size_t i = 0; i < columns.size(); ++i) {
            if (columns[i].name != tag) continue;
            const Rewrite::Cell& cell = row.cells[i];
            if (!cell.hole)
                return Unexpected(
                    refusal(path, "row " + row.key + " already has a cell for " + tag));
            return Change{cell.begin, cell.end, cell_text(**entry, is_column)};
        }
        if (!options.trailing_children)
            return Unexpected(refusal(path, "row " + row.key + " has no column for " + tag +
                                                ": add it to the table's #columns"));
        return plain;
    }

    // Between the rows of a headed table: the header in force at `at`.
    const Rewrite::Header* header = nullptr;
    for (const Rewrite::Header& h : rw.headers)
        if (h.table == inner && h.at < at) header = &h;
    if (!header) return plain;
    auto entry = entry_of(inserted, path);
    if (!entry) return Unexpected(std::move(entry).error());
    const Array& row = **entry;
    const std::vector<Column>& columns = header->columns;
    auto is_column = [&](Symbol s) {
        return std::any_of(columns.begin(), columns.end(),
                           [&](const Column& c) { return c.name == s.str(); });
    };
    std::string text = "(" + to_string(row[0]);
    for (const Column& col : columns) {
        const Array* field = row.try_find(Symbol(col.name));
        text += ' ';
        text += field ? cell_text(*field, is_column) : std::string(kHole);
    }
    for (uint32_t i = 1; i < row.size(); ++i) {
        if (const Array* f = row[i].try_array(); f && is_column(f->tag())) continue;
        if (!options.trailing_children)
            return Unexpected(refusal(
                path, "the new row " + to_string(row[0]) +
                          " has a field the table has no column for: " + to_string(row[i])));
        text += ' ';
        text += to_string(row[i]);
    }
    text += ')';
    return Change{at, at, std::string(ws) + text};
}

}  // namespace

HeadedFiles::HeadedFiles(ReadOptions options) : HeadedFiles(disk_load, disk_store, options) {}

HeadedFiles::HeadedFiles(Load load, Store store, ReadOptions options)
    : load_(std::move(load)), store_(std::move(store)), options_(options) {}

HeadedFiles::~HeadedFiles() = default;

void HeadedFiles::attach(Config& config) {
    config.session().set_reader(reader());
    config.set_writer(writer());
}

LoadSession::Reader HeadedFiles::reader() {
    return [this](std::string_view path, TextSink out) {
        std::string text;
        if (!load(path, text)) return false;
        out(text);
        return true;
    };
}

Config::Writer HeadedFiles::writer() {
    return [this](std::string_view path, std::string_view text) { return store(path, text); };
}

bool HeadedFiles::load(std::string_view path, std::string& out) {
    std::string text;
    if (!load_(path, text)) return false;
    auto rw = rewrite(text, path, options_);
    if (!rw) {
        error_ = std::move(rw).error();
        return false;
    }
    out = rw->text;
    auto& file = files_[std::string(path)];
    if (!file) file = std::make_unique<File>();
    file->original = std::move(text);
    file->rewrite = std::move(*rw);
    return true;
}

// What the Config writes is the rewritten text with one change: a value's tokens replaced
// or an entry inserted. Find that change, map it onto the text as written, and check that
// the result reads to the tree the Config meant before writing it.
bool HeadedFiles::store(std::string_view path, std::string_view next) {
    const auto it = files_.find(path);
    if (it == files_.end() || it->second->rewrite.edits.empty()) return store_(path, next);
    File& file = *it->second;
    const std::string& d = file.rewrite.text;
    const std::vector<Edit>& edits = file.rewrite.edits;
    auto fail = [&](Error e) {
        error_ = std::move(e);
        return false;
    };

    size_t p = 0;
    while (p < d.size() && p < next.size() && d[p] == next[p]) ++p;
    size_t smax = 0;
    while (smax < d.size() && smax < next.size() &&
           d[d.size() - 1 - smax] == next[next.size() - 1 - smax])
        ++smax;
    const size_t s = std::min(smax, std::min(d.size(), next.size()) - p);
    size_t a = p, b = d.size() - s;
    size_t ta = p, tb = next.size() - s;
    if (a == b && tb > ta) {
        // An insertion is equally a change at any place it could slide to: take the first
        // where it begins with the space or newline write-back puts before an entry, then
        // move it past the holes after it, so an entry goes after a row's last cell.
        const size_t len = tb - ta;
        for (size_t x = d.size() - std::min(smax, d.size()); x <= p; ++x)
            if (next[x] == ' ' || next[x] == '\n') {
                a = b = ta = x;
                tb = x + len;
                break;
            }
        size_t y = a;
        std::optional<size_t> past;
        while (y < d.size()) {
            if (auto e = blank_end(edits, y)) {
                past = y = *e;
            } else if (is_space(d[y])) {
                ++y;
            } else {
                break;
            }
        }
        if (past) a = b = *past;
    }
    if (a < b && !copied(edits, a, b))
        return fail(refusal(path, "this change to " + std::string(path) +
                                      " spans text the table's rewrite made (a column's name or a "
                                      "hole): write-back into a headed table changes one cell, one "
                                      "row or one entry at a time"));
    const std::string_view inserted = next.substr(ta, tb - ta);
    Change change{to_original(edits, a), a == b ? 0 : to_original(edits, b), std::string(inserted)};
    if (a == b) {
        change.end = change.begin;
        if (!inserted.empty()) {
            auto placed = place(file.rewrite, options_, change.begin, inserted, path);
            if (!placed) return fail(std::move(placed).error());
            change = std::move(*placed);
        }
    }
    if (change.begin > change.end || change.end > file.original.size())
        return fail(refusal(path, "cannot place this change in " + std::string(path)));
    std::string out = file.original;
    out.replace(change.begin, change.end - change.begin, change.text);

    // The check: both texts read alone (their includes through this object) must agree,
    // fields in any order. Text that reads only over the layers below it is not checked.
    LoadSession meant_session;
    meant_session.set_reader(reader());
    auto meant = read_string(next, path, meant_session);
    LoadSession got_session;
    got_session.set_reader(reader());
    auto got = read(out, path, got_session, options_);
    if (meant && !got)
        return fail(refusal(path, "the change does not read back in the table's form: " +
                                      std::string(to_string(got.error()))));
    if (meant && got) {
        const std::string diff = first_difference(*canonical(**meant), *canonical(**got));
        if (!diff.empty())
            return fail(
                refusal(path, "the change reads back differently in the table's form, at " + diff));
    }
    if (!store_(path, out)) return false;
    auto rw = rewrite(out, path, options_);
    if (rw) {
        file.original = std::move(out);
        file.rewrite = std::move(*rw);
    }
    return true;
}

// ---------------------------------------------------------------------------
// The printer.

namespace {

char open_of(Type t) noexcept {
    return t == Type::Command ? '{' : t == Type::Property ? '[' : '(';
}
char close_of(Type t) noexcept {
    return t == Type::Command ? '}' : t == Type::Property ? ']' : ')';
}

// A field a cell can hold: values with no array among them, or one command or property.
bool flat_field(const Array& f) {
    if (f.size() == 2 && (f[1].type() == Type::Command || f[1].type() == Type::Property))
        return true;
    for (uint32_t i = 1; i < f.size(); ++i)
        if (f[i].is_array_like()) return false;
    return true;
}

bool usable_tag(const Array& a) {
    const Symbol tag = a.tag();
    return !tag.str().empty() && tag.str() != kHole;
}

// A keyed table (D142): a tagged array of two or more keyed records whose children are all
// tagged arrays.
bool candidate(const Array& a) {
    if (a.size() < 3 || !usable_tag(a)) return false;
    for (uint32_t i = 1; i < a.size(); ++i) {
        if (a[i].type() != Type::Array) return false;
        const Array& row = *a[i].array();
        if (!usable_tag(row)) return false;
        for (uint32_t j = 1; j < row.size(); ++j) {
            if (row[j].type() != Type::Array) return false;
            if (!usable_tag(*row[j].array())) return false;
        }
    }
    return true;
}

// Every field of every row flat, and none a removal marker.
bool all_flat(const Array& a) {
    for (uint32_t r = 1; r < a.size(); ++r)
        for (const Array& f : a[r].array()->subarrays())
            if (!flat_field(f) || (f.size() == 2 && f[1].is(remove_sym()))) return false;
    return true;
}

struct Layout {
    std::vector<Symbol> columns;
    std::vector<std::vector<int32_t>> cells;  // [row][column]: the field's index in the row, or -1
    size_t filled = 0;
    [[nodiscard]] bool is_column(Symbol s) const {
        return std::find(columns.begin(), columns.end(), s) != columns.end();
    }
};

enum class Reject { None, NotCandidate, Order, Coverage, Cell, Repeat, Width };

// `indent` is where the table's rows will start; a row that would not fit the width
// even unaligned makes the whole table nested text, since a table is one row a line.
Reject layout_of(const Array& a, const PrintOptions& o, size_t indent, Layout& out) {
    if (!candidate(a)) return Reject::NotCandidate;
    if (!all_flat(a)) return Reject::Cell;
    const uint32_t rows = a.size() - 1;
    std::vector<Symbol> names;  // in first appearance
    std::unordered_map<Symbol, int> index;
    std::vector<std::pair<int, int>> edges;  // field x is written before field y in some row
    for (uint32_t r = 0; r < rows; ++r) {
        const Array& row = *a[r + 1].array();
        std::vector<int> seen;
        for (uint32_t j = 1; j < row.size(); ++j) {
            const Array& f = *row[j].array();
            auto [it, fresh] = index.try_emplace(f.tag(), static_cast<int>(names.size()));
            if (fresh) names.push_back(f.tag());
            const int col = it->second;
            if (std::find(seen.begin(), seen.end(), col) != seen.end()) return Reject::Repeat;
            if (!seen.empty()) edges.emplace_back(seen.back(), col);
            seen.push_back(col);
        }
    }
    // One order every row agrees with, earliest appearance first (Kahn's algorithm).
    const int n = static_cast<int>(names.size());
    std::vector<int> indeg(n, 0);
    std::vector<std::vector<int>> succ(n);
    for (const auto& [x, y] : edges) {
        succ[x].push_back(y);
        ++indeg[y];
    }
    std::vector<int> order;
    std::vector<bool> done(n, false);
    while (static_cast<int>(order.size()) < n) {
        int pick = -1;
        for (int i = 0; i < n && pick < 0; ++i)
            if (!done[i] && indeg[i] == 0) pick = i;
        if (pick < 0) return Reject::Order;
        done[pick] = true;
        order.push_back(pick);
        for (const int y : succ[pick]) --indeg[y];
    }
    std::vector<int> position(n);
    for (int i = 0; i < n; ++i) position[order[i]] = i;
    out.columns.resize(n);
    for (int i = 0; i < n; ++i) out.columns[i] = names[order[i]];
    out.cells.assign(rows, std::vector<int32_t>(n, -1));
    out.filled = 0;
    for (uint32_t r = 0; r < rows; ++r) {
        const Array& row = *a[r + 1].array();
        for (uint32_t j = 1; j < row.size(); ++j) {
            out.cells[r][position[index[row[j].array()->tag()]]] = static_cast<int32_t>(j);
            ++out.filled;
        }
    }
    if (static_cast<double>(out.filled) < o.min_coverage * rows * n) return Reject::Coverage;
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    auto is_column = [&](Symbol s) { return out.is_column(s); };
    for (uint32_t r = 0; r < rows; ++r) {
        const Array& row = *a[r + 1].array();
        size_t len = 2 + cp_len(to_string(row[0]));
        for (int c = 0; c < n; ++c) {
            const int32_t j = out.cells[r][c];
            len += 1 + (j < 0 ? size_t{1} : cp_len(cell_text(*row[j].array(), is_column)));
        }
        if (indent + step + len > width) return Reject::Width;
    }
    return Reject::None;
}

void pad(std::string& out, size_t width, size_t len) {
    if (width > len) out.append(width - len, ' ');
}

void put_table(std::string& out, const Array& a, const Layout& layout, size_t indent,
               const PrintOptions& o) {
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const uint32_t rows = a.size() - 1;
    const size_t n = layout.columns.size();
    auto is_column = [&](Symbol s) { return layout.is_column(s); };
    std::vector<std::string> keys(rows), names(n);
    std::vector<std::vector<std::string>> cells(rows, std::vector<std::string>(n));
    for (size_t c = 0; c < n; ++c) names[c] = to_string(Node(layout.columns[c]));
    size_t keyw = 0;
    std::vector<size_t> cellw(n, 0), namew(n, 0);
    for (uint32_t r = 0; r < rows; ++r) {
        const Array& row = *a[r + 1].array();
        keys[r] = to_string(row[0]);
        keyw = std::max(keyw, cp_len(keys[r]));
        for (size_t c = 0; c < n; ++c) {
            const int32_t j = layout.cells[r][c];
            cells[r][c] = j < 0 ? std::string(kHole) : cell_text(*row[j].array(), is_column);
            cellw[c] = std::max(cellw[c], cp_len(cells[r][c]));
        }
    }
    for (size_t c = 0; c < n; ++c) namew[c] = std::max(cellw[c], cp_len(names[c]));

    // Three forms, the first whose rows fit the width: the names over the cells (the key
    // column at least 8 wide, so the first cell starts after "#columns ("), the cells
    // aligned among themselves under a compact header, and single spaces. The header may
    // wrap: only its '(' must be on the directive's line.
    auto render = [&](bool with_names, bool aligned) {
        std::vector<std::string> lines;
        const size_t key_width = with_names ? std::max<size_t>(keyw, 8) : keyw;
        const std::vector<size_t>& w = with_names ? namew : cellw;
        std::string h(kHeader);
        h.append(with_names ? key_width - 7 : 1, ' ');
        h += '(';
        const size_t hang = cp_len(h);
        for (size_t c = 0; c < n; ++c) {
            if (c > 0 && !with_names && indent + step + cp_len(h) + 1 + cp_len(names[c]) > width) {
                lines.push_back(std::move(h));
                h.assign(hang, ' ');
            } else if (c > 0) {
                h += ' ';
            }
            h += names[c];
            if (with_names && c + 1 < n) pad(h, w[c], cp_len(names[c]));
        }
        h += ')';
        lines.push_back(std::move(h));
        for (uint32_t r = 0; r < rows; ++r) {
            std::string line = "(" + keys[r];
            if (aligned) pad(line, key_width, cp_len(keys[r]));
            for (size_t c = 0; c < n; ++c) {
                line += ' ';
                line += cells[r][c];
                if (aligned && c + 1 < n) pad(line, w[c], cp_len(cells[r][c]));
            }
            line += ')';
            lines.push_back(std::move(line));
        }
        return lines;
    };
    auto fits = [&](const std::vector<std::string>& lines) {
        for (const std::string& l : lines)
            if (indent + step + cp_len(l) > width) return false;
        return true;
    };
    std::vector<std::string> lines;
    if (o.align && fits(lines = render(true, true))) {
    } else if (o.align && fits(lines = render(false, true))) {
    } else {
        lines = render(false, false);
    }
    out += '(';
    out += to_string(a[0]);
    for (const std::string& l : lines) {
        out += '\n';
        out.append(indent + step, ' ');
        out += l;
    }
    out += ')';
}

void put_node(std::string& out, const Node& n, size_t indent, const PrintOptions& o) {
    if (!n.is_array_like()) {
        out += to_string(n);
        return;
    }
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const std::string flat = to_string(n);
    if (indent + cp_len(flat) <= width) {
        out += flat;
        return;
    }
    if (o.tables && n.type() == Type::Array) {
        Layout layout;
        if (layout_of(*n.array(), o, indent, layout) == Reject::None) {
            put_table(out, *n.array(), layout, indent, o);
            return;
        }
    }
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const Array& a = *n.array();
    out += open_of(n.type());
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) {
            out += '\n';
            out.append(indent + step, ' ');
        }
        put_node(out, a[i], indent + step, o);
    }
    out += close_of(n.type());
}

// A table is never inside a flat parent, so its indent is its depth times the step.
void stats_walk(const Array& a, const PrintOptions& o, size_t depth, TableStats& s) {
    if (candidate(a)) {
        ++s.candidates;
        if (all_flat(a)) {
            ++s.flat_candidates;
            s.flat_rows += a.size() - 1;
            for (uint32_t r = 1; r < a.size(); ++r) s.flat_fields += a[r].array()->size() - 1;
        }
        Layout layout;
        const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
        switch (layout_of(a, o, depth * step, layout)) {
            case Reject::None: {
                ++s.tables;
                const size_t rows = a.size() - 1, n = layout.columns.size();
                s.rows += rows;
                s.columns += n;
                s.cells += layout.filled;
                s.holes += rows * n - layout.filled;
                s.names_removed += layout.filled;
                for (uint32_t r = 1; r < a.size(); ++r)
                    for (const Array& f : a[r].array()->subarrays()) {
                        if (f.size() == 2 && f[1].type() != Type::Array) {
                            s.parens_removed += 2;
                            continue;
                        }
                        ++s.list_cells;
                        if (f.size() > 1 && f[1].type() == Type::Symbol) {
                            ++s.list_symbol_first;
                            if (layout.is_column(*f[1].try_sym())) ++s.list_column_first;
                        }
                    }
                break;
            }
            case Reject::Order: ++s.rejected_order; break;
            case Reject::Coverage: ++s.rejected_coverage; break;
            case Reject::Cell: ++s.rejected_cell; break;
            case Reject::Repeat: ++s.rejected_repeat; break;
            case Reject::Width: ++s.rejected_width; break;
            case Reject::NotCandidate: break;
        }
    }
    for (const Node& c : a)
        if (c.is_array_like()) stats_walk(*c.array(), o, depth + 1, s);
}

}  // namespace

// ---------------------------------------------------------------------------

Expected<std::string> desugar(std::string_view text, std::string_view name,
                              const ReadOptions& options) {
    auto rw = rewrite(text, name, options);
    if (!rw) return Unexpected(std::move(rw).error());
    return std::move(rw->text);
}

Expected<Ref<Array>> read(std::string_view text, std::string_view name, LoadSession& session,
                          const ReadOptions& options) {
    if (text.find(kHeader) == std::string_view::npos) return read_string(text, name, session);
    auto rw = rewrite(text, name, options);
    if (!rw) return Unexpected(std::move(rw).error());
    auto root = read_string(rw->text, name, session);
    if (!root) return Unexpected(relocate(std::move(root).error(), *rw, text, name));
    return root;
}

Expected<Ref<Array>> read(std::string_view text, std::string_view name,
                          const ReadOptions& options) {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    HeadedFiles files(options);  // an included file may have tables of its own
    session.set_reader(files.reader());
    auto root = read(text, name, session, options);
    if (!root && files.last_error()) return Unexpected(*files.last_error());
    return root;
}

std::string print(const Array& root, const PrintOptions& options) {
    std::string out;
    for (const Node& n : root) {
        put_node(out, n, 0, options);
        out += '\n';
    }
    return out;
}

TableStats table_stats(const Array& root, const PrintOptions& options) {
    TableStats s;
    for (const Node& n : root)
        if (n.is_array_like()) stats_walk(*n.array(), options, 0, s);
    return s;
}

}  // namespace baker::experiments::syntax
