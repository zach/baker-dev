// Lookup: paths, sorted mode, and the error messages for failed lookups. The fast paths
// are inline in array.hpp; everything here except resolve() and find_sorted() runs only
// when a lookup has already failed.
#include <moira/array.hpp>
#include <moira/path.hpp>
#include <moira/write.hpp>

#include "text.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <optional>

namespace moira {
namespace {

std::optional<Symbol> tag_of(const Node& n) noexcept {
    const Array* a = n.try_array();
    if (!a || a->empty()) return std::nullopt;
    return (*a)[0].try_sym();
}

// The sorted-mode order: by hash, then text. Both depend only on the symbol's text, so
// the order is the same on every run.
bool tag_less(Symbol a, Symbol b) noexcept {
    if (a.hash() != b.hash()) return a.hash() < b.hash();
    return a != b && a.str() < b.str();
}

// A path segment as text: bare where it reads back as one, otherwise 'quoted'.
void append_name(std::string& out, Symbol s) {
    detail::append_path_name(out, s.str());
}

std::string quoted(Symbol s) {
    return "'" + std::string(s.str()) + "'";
}

std::string label_of(const Path::Segment& seg) {
    return seg.is_index ? "element " + std::to_string(seg.index) : quoted(seg.name);
}

void locate(Error& e, const Array* a) {
    if (!a) return;
    e.file = a->file();
    e.line = a->line();
}

bool ascii_iequal(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
               return lower(x) == lower(y);
           });
}

// "'x' not found", plus a hint for the two likeliest mistakes (D6, D23): the key was
// written as a "string", or with different capitalization.
Error not_found(Symbol tag, std::span<const Node> siblings) {
    std::string msg = quoted(tag) + " not found";
    std::optional<Symbol> variant;
    for (const Node& n : siblings) {
        const Array* a = n.try_array();
        if (!a || a->empty()) continue;
        if (auto s = (*a)[0].try_str(); s && *s == tag.str()) {
            msg += "; \"" + std::string(*s) + "\" at line " + std::to_string(a->line()) +
                   " is a string, but keys are symbols: write " + quoted(tag);
            return make_error(ErrorCode::NotFound, std::move(msg));
        }
        if (auto s = (*a)[0].try_sym(); s && !variant && ascii_iequal(s->str(), tag.str()))
            variant = *s;
    }
    if (variant) msg += "; did you mean " + quoted(*variant) + "?";
    return make_error(ErrorCode::NotFound, std::move(msg));
}

std::string preview_values(const Values& v) {
    std::string out = "(";
    for (size_t i = 0; i < v.size() && i < 4; ++i) {
        if (i) out += ' ';
        out += detail::preview(v[i], 20);
    }
    if (v.size() > 4) out += " ...";
    return out + ")";
}

// `label` was found, but its values are not what `info` asked for.
Error conversion_error(const std::string& label, const Values& v, const detail::ConvertInfo& info) {
    using detail::ConvertKind;
    if (v.empty()) return make_error(ErrorCode::Type, label + " has no value");
    const Node& n = v[0];
    const std::string actual(type_name(n.type()));
    if (n.type() == Type::Command || n.type() == Type::Var)
        return make_error(ErrorCode::Type, label + " is a " + actual +
                                               "; reading it as a value needs moira::script");
    switch (info.kind) {
        case ConvertKind::Int:
        case ConvertKind::Float: {
            const std::string_view want = info.kind == ConvertKind::Int ? "int" : "float";
            if (!n.is_number())
                return make_error(ErrorCode::Type,
                                  label + " is " + actual + ", not " + std::string(want));
            const double f = *n.try_float();
            if (info.kind == ConvertKind::Int && n.type() == Type::Float && std::trunc(f) != f)
                return make_error(ErrorCode::Type, label + " is float, not int");
            return make_error(ErrorCode::Range, label + " value " + detail::preview(n) +
                                                    " is out of range for " +
                                                    std::string(info.name));
        }
        case ConvertKind::Scalar:
            return make_error(ErrorCode::Type,
                              label + " is " + actual + ", not " + std::string(info.name));
        case ConvertKind::Compound: break;
    }
    return make_error(ErrorCode::Type, label + " " + preview_values(v) + " does not read as " +
                                           std::string(info.name));
}

// One path step from `v` (whose array is `arr`, or null for a leaf). False if missing.
bool step(Values& v, const Array*& arr, const Path::Segment& seg) noexcept {
    if (seg.is_index) {
        if (seg.index >= v.size()) return false;
        const Node& n = v[seg.index];
        if (const Array* a = n.try_array()) {
            v = Values(a->nodes(), a);
            arr = a;
        } else {
            v = Values(v.nodes().subspan(seg.index, 1), v.source());
            arr = nullptr;
        }
        return true;
    }
    // The array's children are the span plus, for a tagged array, the tag itself, which is
    // not an array; so searching the whole array is the same and can use sorted mode.
    const Array* a = arr ? arr->try_find(seg.name) : nullptr;
    if (!a) return false;
    v = a->values();
    arr = a;
    return true;
}

}  // namespace

// --- Path ---

Path::Path(std::string_view text) {
    if (!parse(text)) {
        valid_ = false;
        segments_.clear();
        bad_text_ = text;
    }
}

bool Path::parse(std::string_view text) {
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        Segment seg;
        if (text[i] == '\'') {
            const size_t close = text.find('\'', i + 1);
            if (close == std::string_view::npos) return false;
            seg.name = Symbol(text.substr(i + 1, close - i - 1));
            i = close + 1;
        } else {
            size_t j = i;
            while (j < n && text[j] != '.' && text[j] != '/') ++j;
            const std::string_view s = text.substr(i, j - i);
            if (s.empty()) return false;
            if (std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) {
                const auto r = std::from_chars(s.data(), s.data() + s.size(), seg.index);
                if (r.ec != std::errc()) return false;
                seg.is_index = true;
            } else {
                seg.name = Symbol(s);
            }
            i = j;
        }
        segments_.push_back(seg);
        if (i == n) break;
        if (text[i] != '.' && text[i] != '/') return false;
        if (++i == n) return false;  // trailing separator
    }
    return true;
}

std::string Path::to_string(size_t count, char separator) const {
    if (!valid_) return bad_text_;
    std::string out;
    for (size_t i = 0; i < segments_.size() && i < count; ++i) {
        if (i) out += separator;
        if (segments_[i].is_index)
            out += std::to_string(segments_[i].index);
        else
            append_name(out, segments_[i].name);
    }
    return out;
}

bool Path::matches(std::string_view text) const noexcept {
    if (!valid_) return false;
    size_t i = 0, k = 0;
    const size_t n = text.size();
    while (i < n) {
        if (k == segments_.size()) return false;
        const Segment& seg = segments_[k++];
        std::string_view name;
        bool quoted_name = false;
        if (text[i] == '\'') {
            const size_t close = text.find('\'', i + 1);
            if (close == std::string_view::npos) return false;
            name = text.substr(i + 1, close - i - 1);
            quoted_name = true;
            i = close + 1;
        } else {
            size_t j = i;
            while (j < n && text[j] != '.' && text[j] != '/') ++j;
            name = text.substr(i, j - i);
            if (name.empty()) return false;
            i = j;
        }
        const bool digits = !quoted_name && std::all_of(name.begin(), name.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
        if (digits) {
            uint32_t index = 0;
            const auto r = std::from_chars(name.data(), name.data() + name.size(), index);
            if (r.ec != std::errc() || !seg.is_index || seg.index != index) return false;
        } else if (seg.is_index || seg.name.str() != name) {
            return false;
        }
        if (i == n) break;
        if ((text[i] != '.' && text[i] != '/') || ++i == n) return false;
    }
    return k == segments_.size();
}

// --- Array lookup ---

Expected<const Array*> Array::try_find_array(Symbol tag) const {
    const Array* a = try_find(tag);
    if (a && a->size_ > 1)
        if (const Array* v = a->data()[1].try_array()) return v;
    return Unexpected(detail::find_error(*this, tag, a, &detail::kArrayInfo));
}

void Array::sort_for_lookup() {
    std::stable_sort(data(), data() + size_, [](const Node& a, const Node& b) {
        const auto tb = tag_of(b);
        if (!tb) return false;
        const auto ta = tag_of(a);
        return !ta || tag_less(*ta, *tb);
    });
    flags_ = static_cast<uint16_t>(flags_ | kSortedForLookup);
}

const Array* Array::find_sorted(Symbol tag) const noexcept {
    const Node* it = std::partition_point(begin(), end(), [tag](const Node& n) {
        const auto t = tag_of(n);
        return !t || tag_less(*t, tag);
    });
    return it != end() && tag_of(*it) == tag ? it->try_array() : nullptr;
}

namespace detail {

Error find_error(const Array& container, Symbol tag, const Array* found, const ConvertInfo* info,
                 const Values* values) {
    Error e = found && info
                  ? conversion_error(quoted(tag), values ? *values : found->values(), *info)
                  : not_found(tag, container.nodes());
    locate(e, found ? found : &container);
    if (!container.empty())
        if (auto t = container[0].try_sym()) append_name(e.path, *t);
    return e;
}

namespace {
std::atomic<Evaluator> g_evaluator{nullptr};
}  // namespace

void set_evaluator(Evaluator evaluator) noexcept {
    g_evaluator.store(evaluator, std::memory_order_release);
}

Evaluator evaluator() noexcept {
    return g_evaluator.load(std::memory_order_acquire);
}

Expected<bool> evaluate_values(const Values& in, Evaluated& out) {
    auto evaluable = [](const Node& n) {
        return n.type() == Type::Command || n.type() == Type::Var;
    };
    const Evaluator eval = evaluator();
    if (!eval || std::none_of(in.begin(), in.end(), evaluable)) return false;
    out.nodes.assign(in.begin(), in.end());
    for (Node& n : out.nodes) {
        if (!evaluable(n)) continue;
        auto r = eval(n);
        if (!r) return Unexpected(std::move(r).error());
        n = std::move(*r);
    }
    out.values = Values(out.nodes, in.source());
    return true;
}

ResolveStatus resolve(const Array& root, const Path& path, Resolved& out) noexcept {
    if (!path.valid()) return ResolveStatus::Malformed;
    Values v(root.nodes(), &root);
    const Array* arr = &root;
    for (size_t i = 0; i < path.size(); ++i)
        if (!step(v, arr, path[i])) return ResolveStatus::Missing;
    out = {v, arr};
    return ResolveStatus::Ok;
}

Error get_error(const Array& root, const Path& path, const ConvertInfo* info,
                const Values* values) {
    if (!path.valid()) {
        Error e = make_error(ErrorCode::Syntax, "malformed path '" + path.to_string() + "'");
        locate(e, &root);
        return e;
    }
    Values v(root.nodes(), &root);
    const Array* arr = &root;
    for (size_t i = 0; i < path.size(); ++i) {
        const Values before = v;
        const Array* before_arr = arr;
        if (step(v, arr, path[i])) continue;
        const Path::Segment& seg = path[i];
        Error e = seg.is_index ? make_error(ErrorCode::NotFound,
                                            label_of(seg) + " is out of range (" +
                                                std::to_string(before.size()) + " values)")
                               : not_found(seg.name, before.nodes());
        locate(e, before_arr ? before_arr : before.source());
        e.path = path.to_string(i);
        return e;
    }
    // The path resolved, so the value did not convert.
    const std::string label = path.empty() ? "the array" : label_of(path[path.size() - 1]);
    Error e = conversion_error(label, values ? *values : v, info ? *info : kArrayInfo);
    locate(e, v.source());
    e.path = path.to_string(path.empty() ? 0 : path.size() - 1);
    return e;
}

}  // namespace detail
}  // namespace moira
