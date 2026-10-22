#include "syntax.hpp"

#include <baker/std_interop.hpp>

#include <algorithm>
#include <cstdint>
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

// A direct child of a bracket: a token, or a nested bracket (opening at `tok`).
struct Child {
    int32_t tok = 0;
    int32_t bracket = -1;
};

class Rewriter {
public:
    Rewriter(std::string_view text, std::string_view name) : text_(text), file_(name) {}

    Expected<std::string> run() {
        structure_ = read_structure(text_, file_);
        index_brackets();
        // Every bracket that directly holds a #columns is a table, in text order.
        std::vector<int32_t> tables;
        const auto& toks = structure_.tokens;
        for (int32_t i = 0; i < static_cast<int32_t>(toks.size()); ++i) {
            const TextToken& t = toks[i];
            if (t.kind != TextTokenKind::Directive || span(t) != kHeader) continue;
            const int32_t parent = parent_of_[i];
            if (parent < 0)
                return fail(ErrorCode::Directive,
                            "#columns needs an enclosing parenthesized array: it declares the "
                            "columns of that array's rows",
                            t.line, t.begin);
            if (structure_.brackets[parent].open != '(')
                return fail(ErrorCode::Directive,
                            "#columns is written inside a parenthesized array, not inside '" +
                                std::string(1, structure_.brackets[parent].open) + "'",
                            t.line, t.begin);
            if (std::find(tables.begin(), tables.end(), parent) == tables.end())
                tables.push_back(parent);
        }
        for (const int32_t b : tables)
            if (auto r = table(b); !r) return Unexpected(std::move(r).error());
        return apply();
    }

private:
    [[nodiscard]] std::string_view span(const TextToken& t) const {
        return text_.substr(t.begin, t.end - t.begin);
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

    // 1-based, in characters from the line's start, as the parser locates.
    [[nodiscard]] int32_t column_at(uint32_t at) const {
        if (at == 0) return 1;
        size_t start = text_.rfind('\n', at - 1);
        start = start == std::string_view::npos ? 0 : start + 1;
        return static_cast<int32_t>(cp_len(text_.substr(start, at - start))) + 1;
    }

    template <class T = std::string>
    [[nodiscard]] Expected<T> fail(ErrorCode code, const std::string& message, int32_t line,
                                   uint32_t at) const {
        Error e = make_error(code, message, file_, line);
        e.set_column(column_at(at));
        return Unexpected(std::move(e));
    }

    // The k-th Open token is bracket k (read_structure pushes one per Open token and
    // leaves stray closes out), so one pass with a stack maps tokens and brackets.
    void index_brackets() {
        const auto& toks = structure_.tokens;
        const auto nb = structure_.brackets.size();
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
            if (structure_.tokens[i].kind == TextTokenKind::Open && bracket_of_[i] >= 0) {
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
        const TextBracket& br = structure_.brackets[b];
        if (br.tag >= 0) return std::string(symbol_text(structure_.tokens[br.tag]));
        return "the array on line " + std::to_string(br.line);
    }

    // How many children after a directive are its arguments, so a table passes them
    // through untouched: #ifdef NAME, #define NAME (body), #include path, #autorun {cmd}.
    [[nodiscard]] size_t directive_arguments(const std::vector<Child>& kids, size_t k) const {
        const std::string_view d = span(structure_.tokens[kids[k].tok]);
        size_t n = 0;
        if (d == "#ifdef" || d == "#ifndef" || d == "#undef" || d == "#include" ||
            d == "#include_opt" || d == "#require" || d == "#merge" || d == "#autorun" ||
            d == "#define")
            n = 1;
        if (d == "#define" && k + 2 < kids.size() && kids[k + 2].bracket >= 0 &&
            structure_.brackets[kids[k + 2].bracket].open == '(' &&
            structure_.brackets[kids[k + 2].bracket].line ==
                structure_.tokens[kids[k + 1].tok].line)
            n = 2;
        return std::min(n, kids.size() - k - 1);
    }

    Expected<void> header(const std::vector<Child>& kids, size_t k, std::vector<Column>& columns) {
        const TextToken& t = structure_.tokens[kids[k].tok];
        const bool has_list = k + 1 < kids.size() && kids[k + 1].bracket >= 0 &&
                              structure_.brackets[kids[k + 1].bracket].open == '(' &&
                              structure_.brackets[kids[k + 1].bracket].line == t.line;
        if (!has_list)
            return fail<void>(ErrorCode::Directive,
                              "#columns takes a parenthesized list of column names on its line: "
                              "#columns (name ...)",
                              t.line, t.begin);
        const int32_t h = kids[k + 1].bracket;
        if (!structure_.brackets[h].closed)
            return fail<void>(ErrorCode::Syntax, "the #columns list is not closed", t.line,
                              t.begin);
        columns.clear();
        for (const Child& c : children(h)) {
            const TextToken& nt = structure_.tokens[c.tok];
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
        edits_.push_back({t.begin, structure_.brackets[h].end, {}, true});
        return {};
    }

    Expected<void> table(int32_t b) {
        const std::vector<Child> kids = children(b);
        std::vector<Column> columns;
        bool headed = false;
        for (size_t k = 0; k < kids.size();) {
            const Child& c = kids[k];
            const TextToken& t = structure_.tokens[c.tok];
            if (c.bracket >= 0) {
                if (headed)
                    if (auto r = row(c.bracket, columns, b); !r) return r;
                ++k;
                continue;
            }
            if (t.kind == TextTokenKind::Directive) {
                if (span(t) == kHeader) {
                    if (auto r = header(kids, k, columns); !r) return r;
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

    Expected<void> row(int32_t r, const std::vector<Column>& columns, int32_t table) {
        const TextBracket& rb = structure_.brackets[r];
        if (rb.open != '(')
            return fail<void>(ErrorCode::Syntax,
                              "under #columns every child of " + bracket_name(table) +
                                  " is a row, (key cells...), not a " +
                                  (rb.open == '{' ? "command" : "property"),
                              rb.line, rb.begin);
        const std::vector<Child> kids = children(r);
        if (kids.empty() || kids[0].bracket >= 0 ||
            structure_.tokens[kids[0].tok].kind == TextTokenKind::Macro ||
            !is_symbol(structure_.tokens[kids[0].tok]))
            return fail<void>(
                ErrorCode::Syntax,
                "a row under #columns starts with its key, a symbol" +
                    std::string(kids.empty() ? ""
                                : kids[0].bracket >= 0
                                    ? ", not an array"
                                    : ", not " + std::string(span(structure_.tokens[kids[0].tok]))),
                rb.line, rb.begin);
        const TextToken& key = structure_.tokens[kids[0].tok];
        const std::string key_text(span(key));
        if (is_hole(key))
            return fail<void>(ErrorCode::Syntax, "a row's key cannot be '_'", key.line, key.begin);
        const size_t n = columns.size();
        const size_t cells = kids.size() - 1;
        // (key #remove), a layer's removal, is not a row of cells and passes through.
        if (cells == 1 && kids[1].bracket < 0 &&
            structure_.tokens[kids[1].tok].kind == TextTokenKind::Directive &&
            span(structure_.tokens[kids[1].tok]) == kRemove)
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
        for (size_t i = 0; i < n; ++i) {
            const Child& c = kids[1 + i];
            const Column& col = columns[i];
            if (c.bracket >= 0) {
                const TextBracket& cb = structure_.brackets[c.bracket];
                if (!cb.closed)
                    return fail<void>(
                        ErrorCode::Syntax,
                        "row " + key_text + ": the cell for " + col.name + " is not closed",
                        cb.line, cb.begin);
                if (cb.open == '(') {
                    insert(cb.begin + 1, col.written + " ");
                } else {
                    insert(cb.begin, "(" + col.written + " ");
                    insert(cb.end, ")");
                }
                continue;
            }
            const TextToken& t = structure_.tokens[c.tok];
            if (t.kind == TextTokenKind::Directive)
                return fail<void>(ErrorCode::Syntax,
                                  "row " + key_text + ": " + std::string(span(t)) +
                                      " cannot be a cell (the cell for " + col.name + ")",
                                  t.line, t.begin);
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
                const TextToken& t = structure_.tokens[c.tok];
                if (is_hole(t))
                    return fail<void>(ErrorCode::Syntax,
                                      "'_' after the " + std::to_string(n) + " cells of row " +
                                          key_text + ": #columns has no column for it",
                                      t.line, t.begin);
                continue;
            }
            const TextBracket& cb = structure_.brackets[c.bracket];
            if (cb.open != '(' || cb.tag < 0) continue;
            const std::string_view tag = symbol_text(structure_.tokens[cb.tag]);
            for (const Column& col : columns)
                if (col.name == tag)
                    return fail<void>(ErrorCode::Syntax,
                                      "row " + key_text + " writes " + col.name +
                                          " as a cell and as a child; write it once, as the cell",
                                      cb.line, cb.begin);
        }
        return {};
    }

    [[nodiscard]] std::string apply() {
        std::stable_sort(edits_.begin(), edits_.end(),
                         [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
        std::string out;
        out.reserve(text_.size() + edits_.size() * 8);
        uint32_t pos = 0;
        for (const Edit& e : edits_) {
            if (e.begin < pos) continue;  // overlapping edits cannot arise; left out if they did
            out.append(text_, pos, e.begin - pos);
            if (e.blank) {
                for (uint32_t i = e.begin; i < e.end; ++i) out += text_[i] == '\n' ? '\n' : ' ';
            } else {
                out += e.text;
            }
            pos = e.end;
        }
        out.append(text_, pos, std::string_view::npos);
        return out;
    }

    std::string_view text_;
    Symbol file_;
    TextStructure structure_;
    std::vector<int32_t> open_tok_, close_tok_;  // per bracket: its Open and Close tokens
    std::vector<int32_t> bracket_of_;            // per token: its bracket, for an Open
    std::vector<int32_t> parent_of_;             // per token: the bracket it is in
    std::vector<Edit> edits_;
};

// ---------------------------------------------------------------------------
// The printer.

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

// A field as a cell: its one atom, command or property, or its values in parentheses.
// The symbol _ as a value is quoted, since bare it would read as a hole.
std::string cell_text(const Array& f) {
    if (f.size() == 2 && !f[1].is_array_like())
        return f[1].is(hole_sym()) ? std::string("'_'") : to_string(f[1]);
    if (f.size() == 2 && f[1].type() != Type::Array) return to_string(f[1]);
    std::string s = "(";
    for (uint32_t i = 1; i < f.size(); ++i) {
        if (i > 1) s += ' ';
        s += to_string(f[i]);
    }
    return s + ")";
}

struct Layout {
    std::vector<Symbol> columns;
    std::vector<std::vector<int32_t>> cells;  // [row][column]: the field's index in the row, or -1
    size_t filled = 0;
};

enum class Reject { None, NotCandidate, Order, Coverage, Cell, Repeat, Width };

// `indent` is where the table's rows will start; a row that would not fit the width
// even unaligned makes the whole table nested text, since a table is one row a line.
Reject layout_of(const Array& a, const PrintOptions& o, size_t indent, Layout& out) {
    if (!candidate(a)) return Reject::NotCandidate;
    const uint32_t rows = a.size() - 1;
    std::vector<Symbol> names;  // in first appearance
    std::unordered_map<Symbol, int> index;
    std::vector<std::pair<int, int>> edges;  // field x is written before field y in some row
    for (uint32_t r = 0; r < rows; ++r) {
        const Array& row = *a[r + 1].array();
        std::vector<int> seen;
        for (uint32_t j = 1; j < row.size(); ++j) {
            const Array& f = *row[j].array();
            if (!flat_field(f) || (f.size() == 2 && f[1].is(remove_sym()))) return Reject::Cell;
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
    for (uint32_t r = 0; r < rows; ++r) {
        const Array& row = *a[r + 1].array();
        size_t len = 2 + cp_len(to_string(row[0]));
        for (int c = 0; c < n; ++c) {
            const int32_t j = out.cells[r][c];
            len += 1 + (j < 0 ? size_t{1} : cp_len(cell_text(*row[j].array())));
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
            cells[r][c] = j < 0 ? std::string(kHole) : cell_text(*row[j].array());
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
                    for (const Array& f : a[r].array()->subarrays())
                        if (f.size() == 2 && f[1].type() != Type::Array) s.parens_removed += 2;
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

Expected<std::string> desugar(std::string_view text, std::string_view name) {
    if (text.find(kHeader) == std::string_view::npos) return std::string(text);
    return Rewriter(text, name).run();
}

Expected<Ref<Array>> read(std::string_view text, std::string_view name, LoadSession& session) {
    if (text.find(kHeader) == std::string_view::npos) return read_string(text, name, session);
    auto rewritten = Rewriter(text, name).run();
    if (!rewritten) return Unexpected(std::move(rewritten).error());
    return read_string(*rewritten, name, session);
}

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    if (text.find(kHeader) == std::string_view::npos) return read_string(text, name);
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    return read(text, name, session);
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
