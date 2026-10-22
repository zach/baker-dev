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
    bool values;  // holds a scalar value beyond its tag: a deeper atom-first line is an error
};

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
        for (;;) {
            if (!skip_blank()) return error_;
            if (pos_ >= src_.size()) break;
            if (!token(shifts)) return error_;
        }
        while (!open_.empty()) close_line();
        return out_;
    }

private:
    // Whitespace and comments, copied as they are. A newline outside a token starts a line.
    bool skip_blank() {
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
        return true;
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

    void close_line() {
        out_.insert(last_token_out_, 1, ')');
        ++last_token_out_;
        open_.pop_back();
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

        bool opened_line = false;
        bool tag = false;
        if (first && depth_ == 0) {
            if (src_.substr(line_start_, indent).find('\t') != std::string_view::npos)
                return fail("a tab in a line's indentation: indent with spaces", begin);
            // (1) The indent closes every open line at or past it; a dedent lands on an open
            // line's indent or the margin.
            uint32_t popped = UINT32_MAX;
            while (!open_.empty() && open_.back().indent >= indent) {
                popped = open_.back().indent;
                close_line();
            }
            if (popped != UINT32_MAX && indent != popped && indent != 0)
                return fail("indent " + std::to_string(indent) + " matches no open line", begin);
            // (2) An atom opens a line; (3) a bracket opens none; (4) a directive opens none.
            if (kind == Atom) {
                const std::string_view text = atom_text(begin);
                const bool symbol = c != '"' && c != '$' && !number_like(text);
                if (c != '\'' && symbol && is_macro(text)) {
                    // A macro name stands for its nodes, which splice in as today.
                } else {
                    if (!open_.empty() && open_.back().values)
                        return fail(
                            "a value list continues on the same line or in brackets; a deeper "
                            "line is a child",
                            begin);
                    if (indent > 0 && src_[begin - 1] == ' ') {
                        out_.back() = '(';
                    } else {
                        out_ += '(';
                        shifts.push_back({line_, column_of(begin)});
                    }
                    if (open_.empty()) ++top_level_lines_;
                    open_.push_back({indent, false});
                    opened_line = true;
                    tag = symbol;
                }
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
            end = begin + atom_text(begin).size();
        }
        if (kind == Open) ++depth_;
        if (kind == Close && depth_ > 0) --depth_;
        const std::string_view text = src_.substr(begin, end - begin);
        if (kind == Directive && text == "#lines") {
            out_.append(text.size(), ' ');  // the translator's layout is always on
        } else {
            out_.append(text);
        }
        if (kind == Directive) {
            naming_ = text == "#define";
            directive_line_ = line_;  // the rest of the line is the directive's arguments
        }
        if (kind == Atom && directive_line_ != line_) {
            if (naming_ && c != '"' && c != '\'' && c != '$') macros_.emplace_back(text);
            naming_ = false;
            // A value of the innermost line: any atom that is not the line's own tag.
            if (depth_ == 0 && !open_.empty() && (!opened_line || !tag) &&
                (opened_line || !(first && is_macro(text) && c != '\'')))
                open_.back().values = true;
        }
        if (kind != Atom && kind != Directive) naming_ = kind == Open && naming_;
        pos_ = end;
        prev_end_ = end;
        last_token_out_ = out_.size();
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
    size_t last_token_out_ = 0;  // where the last token ended in out_: where a line closes
    int32_t line_ = 1;
    int32_t directive_line_ = 0;  // the line of the last directive: its arguments are not values
    int depth_ = 0;               // explicit brackets open: inside them indentation means nothing
    std::vector<OpenLine> open_;
    std::vector<std::string> macros_;  // names this text defines, which splice as today
    bool naming_ = false;              // the next symbol names a macro
    size_t top_level_lines_ = 0;
    Expected<std::string> error_;
};

}  // namespace

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    std::vector<Shift> shifts;
    auto bracketed = Layout(text, name).run(shifts);
    if (!bracketed) return Unexpected(std::move(bracketed).error());
    auto root = read_string(*bracketed, name);
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

std::string print(const Array& root) {
    std::string out;
    for (const Node& n : root) {
        if (tagged(n))
            put_line(out, n, 0, is_section(*n.array()));
        else
            put_pretty(out, n, 0);
        out += '\n';
    }
    return out;
}

}  // namespace baker::experiments::syntax
