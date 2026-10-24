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

// `marker`: the slot is a key's only value, where #remove reads back bare (D143). `quote`:
// an operator at an odd slot of a prefix command, which must not read back as infix.
void put_symbol(Str& out, std::string_view s, bool marker = false, bool quote = false) {
    if (!quote && (detail::is_bare_symbol(s) || (marker && s == detail::remove_marker().str()))) {
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

// How `n`, an array-like node, prints: a command by its infix shape
//; every other array as today.
detail::InfixShape shape_of(const Node& n, const Array& a) noexcept {
    if constexpr (detail::kCurlyInfix)
        return n.type() == Type::Command ? detail::infix_shape(a) : detail::InfixShape::None;
    else
        return detail::InfixShape::None;  // the switch off: main's printer
}
// Whether `a[i]` is an odd slot of a command printed prefix that the reader would take as an
// infix operator: `{+ '-' 1}` quotes it.
bool quote_slot(const Array& a, uint32_t i, detail::InfixShape shape) noexcept {
    return shape == detail::InfixShape::Quoted && (i & 1) && detail::quoted_in_command(a[i]);
}
bool infix(detail::InfixShape shape) noexcept {
    return shape == detail::InfixShape::Chain || shape == detail::InfixShape::Ternary;
}
// What goes between operands i-1 and i of an infix command: the operator, or the ternary's
// `?` and then `:`.
std::string_view separator(const Array& a, detail::InfixShape shape, uint32_t i) noexcept {
    if (shape == detail::InfixShape::Ternary) return i == 2 ? "?" : ":";
    return a[0].try_sym()->str();
}

void put_atom(Str& out, const Node& n, bool marker = false, bool quote = false) {
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
        case Type::Symbol: put_symbol(out, n.try_sym()->str(), marker, quote); break;
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

// Compact form; stops once `out` exceeds `limit` bytes. Returns false if it stopped. An
// infix command prints `{k0 op k2 ...}`: its operator at slot 0 goes between the operands.
bool put_flat(Str& out, const Node& n, size_t limit, bool marker = false, bool quote = false) {
    if (!n.is_array_like()) {
        put_atom(out, n, marker, quote);
        return out.size() <= limit;
    }
    out += open_of(n.type());
    const Array& a = *n.array();
    const detail::InfixShape shape = shape_of(n, a);
    const uint32_t first = infix(shape) ? 1 : 0;
    for (uint32_t i = first; i < a.size(); ++i) {
        if (i > first) {
            out += ' ';
            if (first) {
                out += separator(a, shape, i);
                out += ' ';
            }
        }
        if (!put_flat(out, a[i], limit, marker_slot(n, a, i), quote_slot(a, i, shape)))
            return false;
    }
    out += close_of(n.type());
    return out.size() <= limit;
}

void put_pretty(Str& out, const Node& n, size_t indent, const WriteOptions& o, bool marker = false,
                bool quote = false) {
    if (!n.is_array_like()) {
        put_atom(out, n, marker, quote);
        return;
    }
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const size_t budget = width > indent ? width - indent : 0;
    // The compact form if it fits, written once; else taken back.
    const size_t mark = out.size();
    if (put_flat(out, n, mark + budget, marker, quote)) return;
    out.resize(mark);
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const Array& a = *n.array();
    const detail::InfixShape shape = shape_of(n, a);
    const uint32_t first = infix(shape) ? 1 : 0;
    out += open_of(n.type());
    // One child a line; an infix command's operator ends each line but the last.
    for (uint32_t i = first; i < a.size(); ++i) {
        if (i > first) {
            if (first) {
                out += ' ';
                out += separator(a, shape, i);
            }
            out += '\n';
            out.append(indent + step, ' ');
        }
        put_pretty(out, a[i], indent + step, o, marker_slot(n, a, i), quote_slot(a, i, shape));
    }
    out += close_of(n.type());
}

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
    for (const Node& n : root) {
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
