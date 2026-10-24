#include <baker/write.hpp>

#include "common/containers.hpp"

#include "merge.hpp"
#include "text.hpp"

#include <charconv>
#include <cstdio>

namespace baker {
namespace {

char open_of(Type t) noexcept {
    return t == Type::Command ? '{' : t == Type::Property ? '[' : '(';
}
char close_of(Type t) noexcept {
    return t == Type::Command ? '}' : t == Type::Property ? ']' : ')';
}

// `marker`: the slot is a key's only value, where #remove reads back bare (D143).
void put_symbol(Str& out, std::string_view s, bool marker = false) {
    if (detail::is_bare_symbol(s) || (marker && s == detail::remove_marker().str())) {
        out += s;
    } else {
        out += '\'';
        out += s;
        out += '\'';
    }
}

// Whether `a[i]` is where the parser takes #remove: the second of two in a parenthesized
// array whose first is a symbol.
bool marker_slot(const Node& n, const Array& a, uint32_t i) noexcept {
    return n.type() == Type::Array && a.size() == 2 && i == 1 && a[0].try_sym();
}

void put_atom(Str& out, const Node& n, bool marker = false) {
    char buf[64];
    switch (n.type()) {
        case Type::Int: {
            auto r = std::to_chars(buf, buf + sizeof buf, *n.try_int());
            out.append(buf, r.ptr);
            break;
        }
        case Type::Float: {
            const Str text = detail::format_double(*n.try_float());
            out += text;
            // Keep it a float on reparse: 5 must print as 5.0.
            if (text.find_first_of(".eEni") == Str::npos) out += ".0";
            break;
        }
        case Type::Symbol: put_symbol(out, n.try_sym()->str(), marker); break;
        case Type::String:
            out += '"';
            for (char c : n.string()->view()) {
                if (c == '"')
                    out += "\\q";
                else if (c == '\n')
                    out += "\\n";
                else
                    out += c;
            }
            out += '"';
            break;
        case Type::Var:
            out += '$';
            out += n.var_name().str();
            break;
        case Type::Func: put_symbol(out, n.func_record()->name.str()); break;
        case Type::Object: {
            const ObjectHandle h = n.object_handle();
            out +=
                "'<object " + detail::decimal(h.index()) + ':' + detail::decimal(h.salt()) + ">'";
            break;
        }
        case Type::Array:
        case Type::Command:
        case Type::Property: break;
    }
}

// Compact form; stops once `out` exceeds `limit` bytes. Returns false if it stopped.
bool put_flat(Str& out, const Node& n, size_t limit, bool marker = false) {
    if (!n.is_array_like()) {
        put_atom(out, n, marker);
        return out.size() <= limit;
    }
    out += open_of(n.type());
    const Array& a = *n.array();
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) out += ' ';
        if (!put_flat(out, a[i], limit, marker_slot(n, a, i))) return false;
    }
    out += close_of(n.type());
    return out.size() <= limit;
}

void put_pretty(Str& out, const Node& n, size_t indent, const WriteOptions& o,
                bool marker = false) {
    if (!n.is_array_like()) {
        put_atom(out, n, marker);
        return;
    }
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const size_t budget = width > indent ? width - indent : 0;
    // The compact form if it fits, written once; else taken back.
    const size_t mark = out.size();
    if (put_flat(out, n, mark + budget, marker)) return;
    out.resize(mark);
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const Array& a = *n.array();
    out += open_of(n.type());
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) {
            out += '\n';
            out.append(indent + step, ' ');
        }
        put_pretty(out, a[i], indent + step, o, marker_slot(n, a, i));
    }
    out += close_of(n.type());
}

#if BAKER_INDENT_LINES
bool tagged(const Node& n) noexcept {
    return n.type() == Type::Array && !n.array()->tag().empty();
}

// Whether a top-level array is a section of the document: no scalar values, and a tagged
// array child. Its children are written as lines even when the whole would fit on one.
bool is_section(const Array& a) noexcept {
    bool tagged_child = false;
    for (uint32_t i = 1; i < a.size(); ++i) {
        if (!a[i].is_array_like()) return false;
        tagged_child = tagged_child || tagged(a[i]);
    }
    return tagged_child;
}

// The lines form: `n`, a tagged Array, as `tag values`
// at `indent`, its array children on deeper lines: tagged ones as lines, the rest as rows
// of bracket groups packed to the width. A head holding a value may be followed only by
// groups (a deeper atom-first line under it would read as a continued value list, which
// the parser refuses), and a head that does not fit falls back to the bracketed form.
void put_line(Str& out, const Node& n, size_t indent, const WriteOptions& o, bool section) {
    const Array& a = *n.array();
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const size_t budget = width > indent ? width - indent : 0;
    const size_t mark = out.size();
    auto head = [&](uint32_t count) {  // nodes [0, count) flat, within the budget
        bool fits = true;
        for (uint32_t i = 0; i < count && fits; ++i) {
            if (i > 0) out += ' ';
            fits = put_flat(out, a[i], mark + budget, marker_slot(n, a, i));
        }
        if (!fits) out.resize(mark);
        return fits;
    };
    if (!section && head(a.size())) return;
    uint32_t k = 1;  // the head: the tag and every node up to the last scalar value
    for (uint32_t i = 1; i < a.size(); ++i)
        if (!a[i].is_array_like()) k = i + 1;
    if (k == a.size() || !head(k)) {
        put_pretty(out, n, indent, o);
        return;
    }
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const size_t deeper = indent + step;
    const size_t room = width > deeper ? width - deeper : 0;
    size_t row = SIZE_MAX;  // where the open row of groups begins in `out`; none open
    for (uint32_t i = k; i < a.size(); ++i) {
        const Node& c = a[i];
        if (k == 1 && tagged(c)) {
            row = SIZE_MAX;
            out += '\n';
            out.append(deeper, ' ');
            put_line(out, c, deeper, o, false);
            continue;
        }
        if (row != SIZE_MAX) {  // on the open row, if it fits there
            const size_t at = out.size();
            out += ' ';
            if (put_flat(out, c, row + room)) continue;
            out.resize(at);
        }
        out += '\n';
        out.append(deeper, ' ');
        row = out.size();
        if (put_flat(out, c, row + room)) continue;
        out.resize(row);  // wider than a row: bracketed, over lines
        put_pretty(out, c, deeper, o);
        row = SIZE_MAX;
    }
}
#endif

}  // namespace

void write(const Node& node, TextSink text) {
    Str out;
    put_flat(out, node, SIZE_MAX);
    text(out);
}

void write(const Array& array, TextSink text) {
    Str out = "(";
    bool first = true;
    for (const Node& c : array) {
        if (!first) out += ' ';
        first = false;
        put_flat(out, c, SIZE_MAX);
    }
    out += ')';
    text(out);
}

namespace {
Str dta_text(const Array& root, const WriteOptions& options) {
    Str out;
#if BAKER_INDENT_LINES
    // Where the layout reads the text, a value at the top level goes after the node before
    // it: first on a line it would open one. One that begins the root follows a #lines.
    const bool laid_out = BAKER_INDENT_LINES == 1 || options.lines;
    if ((BAKER_INDENT_LINES == 2 && options.lines) ||
        (laid_out && !root.empty() && !root[0].is_array_like()))
        out += "#lines\n";
#endif
    for (uint32_t i = 0; i < root.size(); ++i) {
        const Node& n = root[i];
#if BAKER_INDENT_LINES
        if (laid_out && !n.is_array_like() && !out.empty() && out.back() == '\n') {
            out.pop_back();
            out += ' ';
        } else if (options.lines && tagged(n) &&
                   (i + 1 == root.size() || root[i + 1].is_array_like())) {
            put_line(out, n, 0, options, is_section(*n.array()));
            out += '\n';
            continue;
        }
#endif
        put_pretty(out, n, 0, options);
        out += '\n';
    }
    // A reader skips a byte order mark at the very start of a file; a symbol that begins
    // with those bytes must not land there.
    if (out.starts_with("\xEF\xBB\xBF")) out.insert(0, " ");
    return out;
}
}  // namespace

void write_dta(const Array& root, TextSink out, const WriteOptions& options) {
    out(dta_text(root, options));
}

Expected<void> write_file(std::string_view path, const Array& root, const WriteOptions& options) {
    const Str text = dta_text(root, options);
    const Str p(path);
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return Unexpected(make_error(ErrorCode::Io, "cannot open '" + p + "' for writing"));
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) return Unexpected(make_error(ErrorCode::Io, "error writing '" + p + "'"));
    return {};
}

namespace detail {
Str preview(const Node& node, size_t max_chars) {
    Str out;
    if (!put_flat(out, node, max_chars) || out.size() > max_chars) {
        out.resize(max_chars > 3 ? max_chars - 3 : 0);
        out += "...";
    }
    return out;
}
}  // namespace detail

}  // namespace baker
