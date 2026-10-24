#include <baker/write.hpp>

#include "common/containers.hpp"

#include "merge.hpp"
#include "text.hpp"

#include <algorithm>
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

// Where the parser reads a symbol specially: a key's only value, where #remove reads back
// bare (D143); with rows, the head of a parenthesized array, where a '/'-led one is a key.
enum Slot : uint8_t { kPlain, kMarker, kHead };

void put_symbol(Str& out, std::string_view s, Slot slot = kPlain) {
    const bool key = slot == kHead && s.size() > 1 && s[0] == '/';
    if ((detail::is_bare_symbol(s) && !key) ||
        (slot == kMarker && s == detail::remove_marker().str())) {
        out += s;
    } else {
        out += '\'';
        out += s;
        out += '\'';
    }
}

// What `a[i]` is to the parser: where it takes #remove, the second of two in a
// parenthesized array whose first is a symbol; with rows, where it takes a key, the first.
Slot slot_of(const Node& n, const Array& a, uint32_t i) noexcept {
    if (n.type() != Type::Array) return kPlain;
    if (a.size() == 2 && i == 1 && a[0].try_sym()) return kMarker;
#ifdef BAKER_PATH_ROWS
    if (i == 0) return kHead;
#endif
    return kPlain;
}

void put_atom(Str& out, const Node& n, Slot slot = kPlain) {
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
        case Type::Symbol: put_symbol(out, n.try_sym()->str(), slot); break;
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
bool put_flat(Str& out, const Node& n, size_t limit, Slot slot = kPlain) {
    if (!n.is_array_like()) {
        put_atom(out, n, slot);
        return out.size() <= limit;
    }
    out += open_of(n.type());
    const Array& a = *n.array();
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) out += ' ';
        if (!put_flat(out, a[i], limit, slot_of(n, a, i))) return false;
    }
    out += close_of(n.type());
    return out.size() <= limit;
}

void put_pretty(Str& out, const Node& n, size_t indent, const WriteOptions& o, Slot slot = kPlain) {
    if (!n.is_array_like()) {
        put_atom(out, n, slot);
        return;
    }
    const size_t width = o.width > 0 ? static_cast<size_t>(o.width) : 0;
    const size_t budget = width > indent ? width - indent : 0;
    // The compact form if it fits, written once; else taken back.
    const size_t mark = out.size();
    if (put_flat(out, n, mark + budget, slot)) return;
    out.resize(mark);
    const size_t step = o.indent > 0 ? static_cast<size_t>(o.indent) : 0;
    const Array& a = *n.array();
    out += open_of(n.type());
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) {
            out += '\n';
            out.append(indent + step, ' ');
        }
        put_pretty(out, a[i], indent + step, o, slot_of(n, a, i));
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
#ifdef BAKER_PATH_ROWS
// A tag as a segment of a row's key, bare or 'quoted' as a Path reads it; empty when no key
// can hold it (a byte that ends an atom, a quote), or, first, when it would open a comment.
Str segment(Symbol tag, bool first) {
    Str out;
    if (std::any_of(tag.str().begin(), tag.str().end(), [](char c) {
            return detail::is_atom_end(static_cast<unsigned char>(c)) || c == '\'';
        }))
        return out;
    detail::append_path_name(out, tag.str());
    if (first && out[0] == '*') out = "'" + out + "'";  // "/*" opens a comment
    return out;
}

// Whether `c`, a child of a row's array `depth` segments deep, is rows of its own: a tagged
// array with a body, whose tag is no tweak's metadata key (D88) and can be a segment.
bool hoisted(const Node& c, size_t depth) {
    static const Symbol meta[] = {"range", "step", "desc", "note", "kind", "group", "one_of"};
    const Array* a = c.try_array();
    return a && a->size() > 1 && !a->tag().empty() && depth < 16 &&
           std::find(std::begin(meta), std::end(meta), a->tag()) == std::end(meta) &&
           !segment(a->tag(), false).empty();
}

// `a`, its key written as `key`: its body (values, metadata, bare tags) as one row, then
// each other tagged child as rows of its own when those end `a` and no tag repeats there;
// else all of `a` in one row. Read back, the rows fold into `a` again.
void put_rows(Str& out, const Array& a, Str& key, size_t depth) {
    size_t split = a.size();  // the first child written as rows of its own
    bool clean = true;
    for (size_t i = 1; i < a.size() && clean; ++i) {
        if (hoisted(a[i], depth))
            split = std::min(split, i);
        else
            clean = split == a.size();
        for (size_t j = 1; j < i && clean; ++j)
            clean = detail::tag_of(a[i]).empty() || detail::tag_of(a[j]) != detail::tag_of(a[i]);
    }
    split = clean ? split : a.size();
    const size_t mark = out.size();
    out += '(';
    out += key;
    for (size_t i = 1; i < split; ++i) {
        out += ' ';
        put_flat(out, a[i], SIZE_MAX, detail::is_removal(a) ? kMarker : kPlain);
    }
    out += ")\n";  // one line, however long: a row is a line of the diff it becomes
    if (split == 1 && a.size() > 1) out.resize(mark);  // no body: the children's rows say it
    for (size_t i = split; i < a.size(); ++i) {
        const size_t k = key.size();
        key += '.';
        key += segment(a[i].try_array()->tag(), false);
        put_rows(out, *a[i].try_array(), key, depth + 1);
        key.resize(k);
    }
}
#endif

Str dta_text(const Array& root, const WriteOptions& options) {
    Str out;
#ifdef BAKER_PATH_ROWS
    bool distinct = true;  // a repeated top-level tag is a list (D31): rows would fold it
    for (size_t i = 0; i < root.size() && distinct; ++i)
        for (size_t j = 0; j < i && distinct; ++j)
            distinct = detail::tag_of(root[i]).empty() ||
                       detail::tag_of(root[i]) != detail::tag_of(root[j]);
    const bool rows = options.rows && distinct;
    // A first form a reader takes as a declaration (D80, D91) is data in a tree; a row
    // spells it, since a row never declares. A tree read from rows has distinct tags.
    static const Symbol pack("pack");
    static const Symbol origin("origin");
    const Array* lead = root.size() > 0 ? root[0].try_array() : nullptr;
    bool first = distinct && lead &&
                 (lead->tag() == pack ||
                  (lead->tag() == origin && lead->size() > 1 && (*lead)[1].type() == Type::String));
#endif
    for (const Node& n : root) {
#ifdef BAKER_PATH_ROWS
        const Array* a = rows || first ? n.try_array() : nullptr;
        first = false;
        if (Str key = a && !a->tag().empty() ? segment(a->tag(), true) : Str(); !key.empty()) {
            key.insert(0, "/");
            put_rows(out, *a, key, 1);
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
