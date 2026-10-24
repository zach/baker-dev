// indent-lines: a line that begins with an atom is an array (dev/syntax-indent-lines-design.md).
//
// The reader is a layout pass over the text that writes the brackets the indentation
// implies, then Baker's own parser: so the tree, its provenance (file and line on every
// array), macros, directives and packs are the core's, and this file holds only the layout
// rule. The pass keeps every line number and nearly every column (a bracket replaces the
// space before an indented line's first token; an unindented line is shifted by one, which
// the error's column is corrected for).
//
// The printer writes each tagged array whose head fits the width as `tag values` on one
// line with its array children as deeper lines, bracket groups for untagged children, and
// falls back to the bracketed pretty form where the line form cannot express an array.
// This is a tool: it uses Baker through the public headers and the STL.
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {

namespace {

// --- The printer: tree -> lines. The house style: indent 3, width 100. ---

constexpr size_t kIndent = 3;
constexpr size_t kWidth = 100;

// The compact form without its outer brackets: `tag v1 v2 (g)`.
std::string inner_text(const Node& n) {
    std::string s = to_string(n);
    return s.size() >= 2 ? s.substr(1, s.size() - 2) : s;
}

bool tagged(const Node& n) {
    return n.type() == Type::Array && !n.array()->tag().empty();
}

// The bracketed pretty form of `n` at `indent`: Baker's own printer, its continuation
// lines indented to match (as config_write.cpp's render_entry does).
void put_pretty(std::string& out, const Node& n, size_t indent) {
    Node copy = n;
    const Ref<Array> root = Array::make_from(std::span<Node>(&copy, 1));
    std::string text =
        to_dta(*root, {.indent = static_cast<int>(kIndent),
                       .width = static_cast<int>(kWidth > indent ? kWidth - indent : 1)});
    while (!text.empty() && text.back() == '\n') text.pop_back();
    const std::string pad(indent, ' ');
    for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 1))
        text.insert(at + 1, pad);
    out += text;
}

// Whether a top-level array is a section of the document: no scalar values, and a tagged
// array child. Its children are written as lines even when the whole would fit on one.
bool is_section(const Array& a) {
    bool tagged_child = false;
    for (size_t i = 1; i < a.size(); ++i) {
        if (!a[i].is_array_like()) return false;
        tagged_child = tagged_child || tagged(a[i]);
    }
    return tagged_child;
}

// `node`, a tagged Array, as a line at `indent` with its children as deeper lines.
void put_line(std::string& out, const Node& node, size_t indent, bool section = false) {
    const Array& a = *node.array();
    const size_t budget = kWidth > indent ? kWidth - indent : 0;
    const std::string flat = inner_text(node);
    if (flat.size() <= budget && !section) {
        out += flat;
        return;
    }
    // The head: the tag and every node up to the last scalar value. The rest, all arrays,
    // go on deeper lines. A head holding a value may be followed only by bracket groups: a
    // deeper atom-first line under it is the continued-value-list error.
    size_t k = 1;
    for (size_t i = 1; i < a.size(); ++i)
        if (!a[i].is_array_like()) k = i + 1;
    if (k == a.size()) {  // nothing to move down: the bracketed form
        put_pretty(out, node, indent);
        return;
    }
    const std::string head =
        k == 1 ? to_string(a[0]) : inner_text(Node(Array::make_copy(a.nodes().subspan(0, k))));
    if (head.size() > budget) {
        put_pretty(out, node, indent);
        return;
    }
    out += head;
    const size_t deeper = indent + kIndent;
    const size_t room = kWidth > deeper ? kWidth - deeper : 0;
    const bool children_may_be_lines = k == 1;
    std::string row;  // bracket groups packed onto one line, as the corpus writes tuple rows
    auto flush_row = [&] {
        if (row.empty()) return;
        out += '\n';
        out.append(deeper, ' ');
        out += row;
        row.clear();
    };
    for (size_t i = k; i < a.size(); ++i) {
        const Node& c = a[i];
        if (children_may_be_lines && tagged(c)) {
            flush_row();
            out += '\n';
            out.append(deeper, ' ');
            put_line(out, c, deeper);
            continue;
        }
        const std::string g = to_string(c);
        if (!row.empty() && row.size() + 1 + g.size() > room) flush_row();
        if (row.empty() && g.size() > room) {  // one group wider than a row: bracketed, over lines
            out += '\n';
            out.append(deeper, ' ');
            put_pretty(out, c, deeper);
            continue;
        }
        if (!row.empty()) row += ' ';
        row += g;
    }
    flush_row();
}

// --- The reader: a layout pass that writes the brackets, then Baker's parser. ---

// The bytes that end an atom, as src/data/text.hpp's kAtomEnd: control characters and
// space, the six brackets, a quote and a comment.
constexpr std::array<bool, 256> kAtomEnd = [] {
    std::array<bool, 256> t{};
    for (int c = 0; c <= 0x20; ++c) t[static_cast<size_t>(c)] = true;
    for (const char c : std::string_view("(){}[]\";")) t[static_cast<unsigned char>(c)] = true;
    return t;
}();

// Whether a bare atom would read as a number (near enough for the layout: only whether a
// line's first atom is a tag depends on it, and the printer never writes a number first).
bool number_like(std::string_view atom) {
    if (atom.empty()) return false;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (digit(atom[0])) return true;
    return atom.size() > 1 && (atom[0] == '-' || atom[0] == '+' || atom[0] == '.') &&
           (digit(atom[1]) || (atom[1] == '.' && atom.size() > 2 && digit(atom[2])));
}

struct Shift {
    int32_t line;
    int32_t column;  // a bracket was inserted at this column: later columns moved by one
};

struct OpenLine {
    uint32_t indent;
    size_t cond;       // the conditionals open when it opened: it closes in their branch
    bool scanned;      // its values were looked for, at its first child line
    bool values;       // it holds a scalar value beyond its tag
    size_t first_out;  // where its own nodes begin in the output, for the scan
};

// The layout as a pass over the text, writing the brackets it implies (the core's
// Parser::layout, BAKER_INDENT_LINES, without evaluating conditionals: a line opened in a
// conditional's branch closes at that branch's end).
class Layout {
public:
    Layout(std::string_view src, std::string_view name) : src_(src), name_(name) {}

    // How many lines the layout opened at the top level: in a bracketed file, each is a
    // bare atom first on its line, which today reads as a value of the root and under the
    // layout as an array (the gate of dev/syntax-indent-lines-design.md).
    [[nodiscard]] size_t top_level_lines() const { return top_level_lines_; }

    // The text with the brackets the layout implies, or a located error.
    Expected<std::string> run(std::vector<Shift>& shifts) {
        out_.reserve(src_.size() + src_.size() / 8);
        if (src_.starts_with("\xEF\xBB\xBF")) {
            out_.append(src_, 0, 3);
            pos_ = line_start_ = 3;
        }
        mark_.push_back(out_.size());
        for (;;) {
            skip_blank();
            if (pos_ >= src_.size()) break;
            if (!token(shifts)) return error_;
        }
        while (!open_.empty()) close_line();
        return out_;
    }

private:
    // Whitespace and comments, copied as they are. A newline outside a token starts a line.
    void skip_blank() {
        const size_t n = src_.size();
        while (pos_ < n) {
            const auto c = static_cast<unsigned char>(src_[pos_]);
            if (c <= 0x20) {
                if (c == '\n') {
                    ++line_;
                    line_start_ = pos_ + 1;
                }
                out_ += static_cast<char>(c);
                ++pos_;
            } else if (c == ';') {
                const size_t end = std::min(src_.find('\n', pos_), n);
                out_.append(src_, pos_, end - pos_);
                pos_ = end;
            } else if (c == '/' && pos_ + 1 < n && src_[pos_ + 1] == '*') {
                size_t end = src_.find("*/", pos_ + 2);
                end = end == std::string_view::npos ? n : end + 2;
                for (size_t i = pos_; i < end; ++i)
                    if (src_[i] == '\n') {
                        ++line_;
                        line_start_ = i + 1;
                    }
                out_.append(src_, pos_, end - pos_);
                pos_ = end;
            } else {
                break;
            }
        }
    }

    int32_t column_of(size_t at) const {
        int32_t column = 1;
        for (size_t i = line_start_; i < at && i < src_.size(); ++i)
            if ((static_cast<unsigned char>(src_[i]) & 0xC0) != 0x80) ++column;
        return column;
    }

    bool fail(std::string_view message, size_t at) {
        Error e = make_error(ErrorCode::Syntax, message, Symbol(name_), line_);
        e.set_column(column_of(at));
        error_ = Unexpected(std::move(e));
        return false;
    }

    // A line closes after the last token of its conditional branch.
    void close_line() {
        size_t& at = mark_[open_.back().cond];
        out_.insert(at, 1, ')');
        for (size_t& m : mark_)
            if (m >= at) ++m;
        open_.pop_back();
    }

    // Whether the open line holds a value beyond its tag: an atom in its own output before
    // its first child line (a group's contents aside).
    bool holds_values(const OpenLine& l) const {
        std::string_view text = std::string_view(out_).substr(l.first_out);
        int depth = 0;
        bool tag = true;
        for (size_t i = 0; i < text.size();) {
            const char c = text[i];
            if (c == ';') {
                i = std::min(text.find('\n', i), text.size());
            } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                const size_t e = text.find("*/", i + 2);
                i = e == std::string_view::npos ? text.size() : e + 2;
            } else if (static_cast<unsigned char>(c) <= 0x20) {
                ++i;
            } else if (c == '(' || c == '{' || c == '[') {
                ++depth;
                ++i;
                tag = false;
            } else if (c == ')' || c == '}' || c == ']') {
                --depth;
                ++i;
            } else {
                size_t e = i + 1;
                if (c == '"' || c == '\'') {
                    e = text.find(c, i + 1);
                    e = e == std::string_view::npos ? text.size() : e + 1;
                } else {
                    while (e < text.size() && !kAtomEnd[static_cast<unsigned char>(text[e])]) ++e;
                }
                const std::string_view atom = text.substr(i, e - i);
                const bool directive = c == '#' && atom.size() > 1;
                if (depth == 0 && !directive) {
                    const bool symbol = c != '"' && c != '$' && !number_like(atom);
                    // A macro's nodes are taken for arrays: the translator reads no bodies.
                    if (!(tag && symbol) && !(symbol && c != '\'' && is_macro(atom))) return true;
                }
                tag = false;
                i = e;
            }
        }
        return false;
    }

    // One token at pos_: the layout decision if it is first on its line outside brackets,
    // then the token copied.
    bool token(std::vector<Shift>& shifts) {
        const size_t begin = pos_;
        const char c = src_[begin];
        const bool first = prev_end_ <= line_start_;
        const auto indent = static_cast<uint32_t>(begin - line_start_);
        enum { Open, Close, Atom, Directive } kind = Atom;
        if (c == '(' || c == '{' || c == '[')
            kind = Open;
        else if (c == ')' || c == '}' || c == ']')
            kind = Close;
        else if (c == '#' && begin + 1 < src_.size() &&
                 !kAtomEnd[static_cast<unsigned char>(src_[begin + 1])])
            kind = Directive;
        const std::string_view word = kind == Atom || kind == Directive ? atom_text(begin) : "";
        const bool conditional = kind == Directive && (word == "#ifdef" || word == "#ifndef" ||
                                                       word == "#else" || word == "#endif");

        if (first && depth_ == 0 && !conditional) {
            const bool symbol = kind == Atom && c != '"' && c != '$' && !number_like(word);
            const bool opens = kind == Atom && !(c != '\'' && symbol && is_macro(word));
            if (opens || !open_.empty()) {
                if (src_.substr(line_start_, indent).find('\t') != std::string_view::npos)
                    return fail("a tab in a line's indentation: indent with spaces", begin);
                // (1) The indent closes every open line at or past it; a dedent lands on an
                // open line's indent, and a top-level line begins at the margin.
                uint32_t popped = UINT32_MAX;
                while (!open_.empty() && open_.back().indent >= indent) {
                    popped = open_.back().indent;
                    close_line();
                }
                if (popped != UINT32_MAX ? indent != popped : opens && open_.empty() && indent)
                    return fail("indent " + std::to_string(indent) + " matches no open line",
                                begin);
            }
            if (opens) {  // (2) an atom opens a line; a bracket, a directive or a macro none
                if (!open_.empty()) {
                    OpenLine& parent = open_.back();
                    if (!parent.scanned) {
                        parent.values = holds_values(parent);
                        parent.scanned = true;
                    }
                    if (parent.values)
                        return fail(
                            "a value list continues on the same line or in brackets; a deeper "
                            "line is a child",
                            begin);
                }
                if (indent > 0 && src_[begin - 1] == ' ') {
                    out_.back() = '(';
                } else {
                    out_ += '(';
                    shifts.push_back({line_, column_of(begin)});
                }
                if (open_.empty()) ++top_level_lines_;
                open_.push_back({indent, mark_.size() - 1, false, false, out_.size()});
            } else if (kind == Open && c == '[' && open_.empty()) {
                naming_ = true;  // [NAME ...] at the top level defines a macro
            }
        }

        // The token itself.
        size_t end = begin + 1;
        if (c == '"' || c == '\'') {
            end = src_.find(c, begin + 1);
            end = end == std::string_view::npos ? src_.size() : end + 1;
            for (size_t i = begin; i < end; ++i)
                if (src_[i] == '\n') ++line_;
        } else if (kind == Atom || kind == Directive) {
            end = begin + word.size();
        }
        if (kind == Open) ++depth_;
        if (kind == Close && depth_ > 0) --depth_;
        const std::string_view text = src_.substr(begin, end - begin);
        bool branch = conditional && text != "#endif";  // a branch begins after it
        if (conditional && (text == "#else" || text == "#endif")) {
            branch = branch && mark_.size() > 1;
            if (mark_.size() > 1) {  // lines opened in the branch that ends close in it
                while (!open_.empty() && open_.back().cond == mark_.size() - 1) close_line();
                mark_.pop_back();
            }
        }
        if (kind == Directive && text == "#lines")
            out_.append(text.size(), ' ');  // the translator's layout is always on
        else
            out_.append(text);
        if (branch) mark_.push_back(out_.size());
        if (kind == Directive) naming_ = text == "#define";
        if (kind == Atom) {
            if (naming_ && c != '"' && c != '\'' && c != '$') macros_.emplace_back(text);
            naming_ = false;
        }
        if (kind != Atom && kind != Directive) naming_ = kind == Open && naming_;
        pos_ = end;
        prev_end_ = end;
        mark_.back() = out_.size();
        return true;
    }

    std::string_view atom_text(size_t begin) const {
        size_t end = begin;
        while (end < src_.size() && !kAtomEnd[static_cast<unsigned char>(src_[end])]) ++end;
        return src_.substr(begin, end - begin);
    }
    bool is_macro(std::string_view name) const {
        for (const std::string& m : macros_)
            if (m == name) return true;
        return false;
    }

    std::string_view src_;
    std::string_view name_;
    std::string out_;
    size_t pos_ = 0;
    size_t line_start_ = 0;
    size_t prev_end_ = 0;  // where the previous token ended: first on a line if before its start
    // Per conditional depth (0 outside any), where the last token of the current branch
    // ended in out_: where a line opened at that depth closes.
    std::vector<size_t> mark_;
    int32_t line_ = 1;
    int depth_ = 0;  // explicit brackets open: inside them indentation means nothing
    std::vector<OpenLine> open_;
    std::vector<std::string> macros_;  // names this text defines, which splice as today
    bool naming_ = false;              // the next symbol names a macro
    size_t top_level_lines_ = 0;
    Expected<std::string> error_;
};

}  // namespace

Expected<Ref<Array>> translate(std::string_view text, std::string_view name) {
    std::vector<Shift> shifts;
    auto bracketed = Layout(text, name).run(shifts);
    if (!bracketed) return Unexpected(std::move(bracketed).error());
    LoadSession session;
#if BAKER_INDENT_LINES
    session.lines = false;  // the text is bracketed now: read as today
#endif
    auto root = read_string(*bracketed, name, session);
    if (root || shifts.empty()) return root;
    // An error on a line whose first token moved by an inserted bracket: back by one.
    const Error& e = root.error();
    for (const Shift& s : shifts)
        if (s.line == e.line() && e.column() > s.column) {
            Error moved = make_error(e.code(), e.message(), e.file(), e.line());
            moved.set_column(e.column() - 1);
            return Unexpected(std::move(moved));
        }
    return root;
}

size_t top_level_lines(std::string_view text) {
    std::vector<Shift> shifts;
    Layout layout(text, "gate.dta");
    (void)layout.run(shifts);  // an error ends the count where it stands
    return layout.top_level_lines();
}

std::string translator_print(const Array& root) {
    std::string out;
    // A value at the top level goes after the node before it (first on a line it would
    // open one); one that begins the root follows #lines, as the core's printer writes it.
    if (!root.empty() && !root[0].is_array_like()) out += "#lines\n";
    for (size_t i = 0; i < root.size(); ++i) {
        const Node& n = root[i];
        if (!n.is_array_like() && !out.empty() && out.back() == '\n') {
            out.back() = ' ';
            out += to_string(n);
        } else if (tagged(n) && (i + 1 == root.size() || root[i + 1].is_array_like())) {
            put_line(out, n, 0, is_section(*n.array()));
        } else {
            put_pretty(out, n, 0);
        }
        out += '\n';
    }
    return out;
}

#if BAKER_INDENT_LINES
// The build reads lines (BAKER_INDENT_LINES): the core's reader and printer.
Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    LoadSession session;
    session.lines = true;  // from the start, in a DIRECTIVE build too
    return read_string(text, name, session);
}
std::string print(const Array& root) {
    WriteOptions options;
    options.lines = true;
    return to_dta(root, options);
}
#else
Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    return translate(text, name);
}
std::string print(const Array& root) {
    return translator_print(root);
}
#endif

}  // namespace baker::experiments::syntax
