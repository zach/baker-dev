#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace baker::experiments::syntax {
namespace {

// The words that become metadata: today's tweak keys (D88, D218), the shape grammar's
// (shape.cpp), and {func}'s (builtins.cpp). tools/migrate_dta.py holds the same lists.
constexpr std::string_view kMetaKeys[] = {"range", "step",  "desc",  "note",
                                          "kind",  "group", "one_of"};
constexpr std::string_view kFieldKeys[] = {"range", "step",  "desc",   "note",
                                           "kind",  "group", "one_of", "optional"};
constexpr std::string_view kShapeKeys[] = {"at", "keyed"};
constexpr std::string_view kFuncKeys[] = {"status", "doc"};

bool among(Symbol tag, std::span<const std::string_view> keys) {
    return std::find(keys.begin(), keys.end(), tag.str()) != keys.end();
}

// The tag of a parenthesized array, else the empty symbol: a {command} has none.
Symbol tag_of(const Node& n) {
    const Array* a = n.try_array();
    return a ? a->tag() : Symbol();
}

bool is_meta(const Node& n) {
    const Symbol t = tag_of(n);
    return !t.empty() && t.str()[0] == '@';
}

// --- The migration over a tree: print() ---

Node convert(const Node& n, bool sigil, bool top);

// A node rebuilt with `children`, keeping its kind, file and line; `sigil` prefixes '@' to
// its tag.
Node rebuild(const Node& n, std::vector<Node> children, bool sigil) {
    const Array& a = *n.array();
    if (sigil) children[0] = Node(Symbol("@" + std::string(a.tag().str())));
    return Node(Array::make_from(children, a.file(), a.line()), n.type());
}

// A field of a shape: (name type metadata...). Its children after the type that are tagged
// with a field key get the sigil; (list T) in the type's place stays.
Node convert_field(const Node& n) {
    const Array& a = *n.array();
    std::vector<Node> out;
    for (uint32_t i = 0; i < a.size(); ++i)
        out.push_back(convert(a[i], i >= 2 && among(tag_of(a[i]), kFieldKeys), false));
    return rebuild(n, std::move(out), false);
}

// A shape declaration: (name (at ...) (keyed) field...). Its (at ...) and (keyed) get the
// sigil; every other tagged child is a field.
Node convert_shape(const Node& n) {
    const Array& a = *n.array();
    std::vector<Node> out;
    for (uint32_t i = 0; i < a.size(); ++i) {
        const Symbol tag = i ? tag_of(a[i]) : Symbol();
        if (among(tag, kShapeKeys))
            out.push_back(convert(a[i], true, false));
        else if (!tag.empty())
            out.push_back(convert_field(a[i]));
        else
            out.push_back(convert(a[i], false, false));
    }
    return rebuild(n, std::move(out), false);
}

Node convert(const Node& n, bool sigil, bool top) {
    const Array* a = n.array();
    if (!a) return n;
    std::vector<bool> mark(a->size(), false);
    if (n.type() == Type::Array && top && a->tag() == "shapes") {
        std::vector<Node> out;
        for (uint32_t i = 0; i < a->size(); ++i)
            out.push_back(i && !tag_of((*a)[i]).empty() ? convert_shape((*a)[i])
                                                        : convert((*a)[i], false, false));
        return rebuild(n, std::move(out), sigil);
    }
    if (n.type() == Type::Command && a->size() >= 3 &&
        ((*a)[0].is("func") || (*a)[0].is("override"))) {
        // (status ...) and (doc ...) between the parameters and the body.
        for (uint32_t i = 3; i < a->size() && among(tag_of((*a)[i]), kFuncKeys); ++i)
            mark[i] = true;
    } else if (n.type() == Type::Array && !a->tag().empty()) {
        // D218: in an array with a value, the trailing run tagged with the metadata keys.
        bool has_value = false;
        for (uint32_t i = 1; i < a->size(); ++i) has_value = has_value || tag_of((*a)[i]).empty();
        if (has_value)
            for (uint32_t i = a->size(); i-- > 1 && among(tag_of((*a)[i]), kMetaKeys);)
                mark[i] = true;
    }
    std::vector<Node> out;
    for (uint32_t i = 0; i < a->size(); ++i) out.push_back(convert((*a)[i], mark[i], false));
    return rebuild(n, std::move(out), sigil);
}

// --- The inverse: strip_sigils() ---

Node strip(const Node& n) {
    const Array* a = n.array();
    if (!a) return n;
    std::vector<Node> out;
    for (const Node& c : *a) out.push_back(strip(c));
    const bool sigil = n.type() == Type::Array && is_meta(n);
    if (sigil) out[0] = Node(Symbol(a->tag().str().substr(1)));
    return Node(Array::make_from(out, a->file(), a->line()), n.type());
}

// --- The placement rule over the text's structure: read() ---

// 1-based column of byte `at`, in code points from its line's start, as the lexer counts.
int32_t column_at(std::string_view text, uint32_t at) {
    const size_t nl = at == 0 ? std::string_view::npos : text.rfind('\n', at - 1);
    int32_t column = 1;
    for (size_t i = nl == std::string_view::npos ? 0 : nl + 1; i < at; ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++column;
    return column;
}

std::string preview_of(std::string_view text, uint32_t begin, uint32_t end) {
    std::string s(text.substr(begin, std::min<size_t>(end - begin, 32)));
    if (end - begin > 32) s += "...";
    return s;
}

// Metadata comes after the value: in every parenthesized array, no child after the first
// (@tag ...) may be untagged (an atom, a {command}, a [property], an array without a symbol
// tag). The first such child, in text order, is the error, at its line and column. Slot 0
// is the tag's place and is never metadata.
std::optional<Error> placement_error(std::string_view text, std::string_view name) {
    const TextStructure st = read_structure(text, Symbol(name));
    struct Open {
        int32_t bracket;
        uint32_t children = 0;
        int32_t first_meta = -1;  // the bracket index of the first metadata child
    };
    std::vector<Open> stack;
    auto tag_text = [&](const TextBracket& b) -> std::string_view {
        if (b.tag < 0) return {};
        const TextToken& t = st.tokens[static_cast<size_t>(b.tag)];
        std::string_view s = text.substr(t.begin, t.end - t.begin);
        if (t.kind == TextTokenKind::QuotedSymbol && s.size() >= 2) s = s.substr(1, s.size() - 2);
        return s;
    };
    auto error = [&](const Open& parent, int32_t line, uint32_t begin, uint32_t end) {
        const TextBracket& meta = st.brackets[static_cast<size_t>(parent.first_meta)];
        Error e = make_error(ErrorCode::Syntax,
                             "metadata comes after the value: move " +
                                 preview_of(text, meta.begin, meta.end) + " after " +
                                 preview_of(text, begin, end),
                             Symbol(name), line);
        e.set_column(column_at(text, begin));
        return e;
    };
    int32_t opened = 0;
    for (const TextToken& t : st.tokens) {
        if (t.kind == TextTokenKind::Close) {
            if (!stack.empty()) stack.pop_back();
            continue;
        }
        Open* parent = stack.empty() ? nullptr : &stack.back();
        const bool in_paren =
            parent && st.brackets[static_cast<size_t>(parent->bracket)].open == '(';
        const uint32_t index = parent ? parent->children++ : 0;
        if (t.kind == TextTokenKind::Open) {
            const int32_t k = opened++;
            const TextBracket& b = st.brackets[static_cast<size_t>(k)];
            const std::string_view tag = b.open == '(' ? tag_text(b) : std::string_view();
            const bool meta = !tag.empty() && tag[0] == '@';
            if (in_paren && index >= 1) {
                if (meta && parent->first_meta < 0) parent->first_meta = k;
                if (tag.empty() && parent->first_meta >= 0)
                    return error(*parent, t.line, b.begin, b.end);
            }
            stack.push_back({k});
            continue;
        }
        if (in_paren && index >= 1 && parent->first_meta >= 0)
            return error(*parent, t.line, t.begin, t.end);
    }
    return std::nullopt;
}

}  // namespace

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    if (auto e = placement_error(text, name)) return Unexpected(std::move(*e));
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    return read_string(text, name, session);
}

std::string print(const Array& root) {
    std::vector<Node> out;
    for (const Node& n : root) out.push_back(convert(n, false, true));
    return to_dta(*Array::make_from(out, root.file(), root.line()), {.indent = 3, .width = 96});
}

Ref<Array> strip_sigils(const Array& root) {
    std::vector<Node> out;
    for (const Node& n : root) out.push_back(strip(n));
    return Array::make_from(out, root.file(), root.line());
}

Expected<Ref<Array>> read_as_dta(std::string_view text, std::string_view name) {
    auto r = read(text, name);
    if (!r) return Unexpected(std::move(r).error());
    return strip_sigils(**r);
}

size_t count_meta_tags(const Array& root) {
    size_t n = 0;
    for (const Node& c : root) {
        if (is_meta(c)) ++n;
        if (const Array* a = c.array()) n += count_meta_tags(*a);
    }
    return n;
}

}  // namespace baker::experiments::syntax
