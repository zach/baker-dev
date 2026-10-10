#include <moira/write.hpp>

#include "text.hpp"

#include <charconv>
#include <cstdio>
#include <string>

namespace moira {
namespace {

char open_of(Type t) noexcept {
    return t == Type::Command ? '{' : t == Type::Property ? '[' : '(';
}
char close_of(Type t) noexcept {
    return t == Type::Command ? '}' : t == Type::Property ? ']' : ')';
}

void put_symbol(std::string& out, std::string_view s) {
    if (detail::is_bare_symbol(s)) {
        out += s;
    } else {
        out += '\'';
        out += s;
        out += '\'';
    }
}

void put_atom(std::string& out, const Node& n) {
    char buf[64];
    switch (n.type()) {
        case Type::Int: {
            auto r = std::to_chars(buf, buf + sizeof buf, *n.try_int());
            out.append(buf, r.ptr);
            break;
        }
        case Type::Float: {
            auto r = std::to_chars(buf, buf + sizeof buf, *n.try_float());
            const std::string_view text(buf, static_cast<size_t>(r.ptr - buf));
            out += text;
            // Keep it a float on reparse: 5 must print as 5.0.
            if (text.find_first_of(".eEni") == std::string_view::npos) out += ".0";
            break;
        }
        case Type::Symbol: put_symbol(out, n.try_sym()->str()); break;
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
            out += "'<object " + std::to_string(h.index()) + ':' + std::to_string(h.salt()) + ">'";
            break;
        }
        case Type::Array:
        case Type::Command:
        case Type::Property: break;
    }
}

// Compact form; stops once `out` exceeds `limit` bytes. Returns false if it stopped.
bool put_flat(std::string& out, const Node& n, size_t limit) {
    if (!n.is_array_like()) {
        put_atom(out, n);
        return out.size() <= limit;
    }
    out += open_of(n.type());
    bool first = true;
    for (const Node& c : *n.array()) {
        if (!first) out += ' ';
        first = false;
        if (!put_flat(out, c, limit)) return false;
    }
    out += close_of(n.type());
    return out.size() <= limit;
}

// Length of the compact form, or anything > budget if it is longer.
size_t flat_length(const Node& n, size_t budget) {
    std::string tmp;
    put_flat(tmp, n, budget);
    return tmp.size();
}

void put_pretty(std::string& out, const Node& n, size_t indent, const WriteOptions& o) {
    if (!n.is_array_like()) {
        put_atom(out, n);
        return;
    }
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const size_t budget = width > indent ? width - indent : 0;
    if (flat_length(n, budget) <= budget) {
        put_flat(out, n, SIZE_MAX);
        return;
    }
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const Array& a = *n.array();
    out += open_of(n.type());
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) {
            out += '\n';
            out.append(indent + step, ' ');
        }
        put_pretty(out, a[i], indent + step, o);
    }
    out += close_of(n.type());
}

}  // namespace

std::string to_string(const Node& node) {
    std::string out;
    put_flat(out, node, SIZE_MAX);
    return out;
}

std::string to_string(const Array& array) {
    std::string out = "(";
    bool first = true;
    for (const Node& c : array) {
        if (!first) out += ' ';
        first = false;
        put_flat(out, c, SIZE_MAX);
    }
    out += ')';
    return out;
}

std::string to_dta(const Array& root, const WriteOptions& options) {
    std::string out;
    for (const Node& n : root) {
        put_pretty(out, n, 0, options);
        out += '\n';
    }
    // A reader skips a byte order mark at the very start of a file; a symbol that begins
    // with those bytes must not land there.
    if (out.starts_with("\xEF\xBB\xBF")) out.insert(out.begin(), ' ');
    return out;
}

Expected<void> try_write_file(std::string_view path, const Array& root,
                              const WriteOptions& options) {
    const std::string text = to_dta(root, options);
    const std::string p(path);
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return Unexpected(make_error(ErrorCode::Io, "cannot open '" + p + "' for writing"));
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) return Unexpected(make_error(ErrorCode::Io, "error writing '" + p + "'"));
    return {};
}

#if MOIRA_EXCEPTIONS
void write_file(std::string_view path, const Array& root, const WriteOptions& options) {
    if (auto r = try_write_file(path, root, options); !r) detail::raise(std::move(r).error());
}
#endif

namespace detail {
std::string preview(const Node& node, size_t max_chars) {
    std::string out;
    if (!put_flat(out, node, max_chars) || out.size() > max_chars) {
        out.resize(max_chars > 3 ? max_chars - 3 : 0);
        out += "...";
    }
    return out;
}
}  // namespace detail

}  // namespace moira
