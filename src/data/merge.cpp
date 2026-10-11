
#include "merge.hpp"
#include "common/containers.hpp"

#include <optional>

namespace moira {
namespace detail {
namespace {

std::optional<Symbol> tag_of(const Array& a) noexcept {
    return a.empty() ? std::nullopt : a[0].try_sym();
}
bool is_tagged(const Node& n) noexcept {
    const Array* a = n.try_array();
    return a && tag_of(*a);
}
// Whether `a` has values from `first` on: nodes other than tagged arrays.
bool has_values(const Array& a, size_t first) noexcept {
    for (size_t i = first; i < a.size(); ++i)
        if (!is_tagged(a[i])) return true;
    return false;
}

}  // namespace

// The GDC 2005 slide shows (a (b 1)) merged with (a (b 2) (c 2)) giving (a (b 1) (c 2)).
// The spec took that for a typo; for #merge it is not, and for Config layers the leaves
// win. Both policies are here; see docs/decisions.md D1.
bool merge_into(Vec<Node>& dst, Vec<SourceSpan>* spans, size_t dst_first, const Array& src,
                size_t src_first, MergePolicy policy, SourceMap* map) {
    bool changed = false;
    if (policy == MergePolicy::Overwrite && has_values(src, src_first)) {
        // The incoming values replace the existing ones; tagged children stay to merge.
        Vec<Node> out(dst.begin(), dst.begin() + static_cast<ptrdiff_t>(dst_first));
        for (size_t j = src_first; j < src.size(); ++j)
            if (!is_tagged(src[j])) out.push_back(src[j]);
        for (size_t i = dst_first; i < dst.size(); ++i)
            if (is_tagged(dst[i])) out.push_back(dst[i]);
        dst.swap(out);
        changed = true;
    }
    for (size_t j = src_first; j < src.size(); ++j) {
        const Array* sa = src[j].try_array();
        if (!sa) continue;
        const auto tag = tag_of(*sa);
        if (!tag && policy == MergePolicy::Overwrite) continue;  // a value, handled above
        size_t found = dst.size();
        if (tag) {
            for (size_t i = dst_first; i < dst.size(); ++i) {
                const Array* da = dst[i].try_array();
                if (da && tag_of(*da) == tag) {
                    found = i;
                    break;
                }
            }
        }
        if (found == dst.size()) {
            dst.push_back(src[j]);
            if (spans) {
                const SourceSpan* sp = map->span(src, j);
                spans->push_back(sp ? *sp : SourceSpan{});
            }
            changed = true;
            continue;
        }
        const Array& da = *dst[found].try_array();
        Vec<Node> merged(da.begin(), da.end());
        Vec<SourceSpan> merged_spans;
        if (map) {
            const auto known = map->spans(da);
            merged_spans = known ? Vec<SourceSpan>(known->data(), known->data() + known->size())
                                 : Vec<SourceSpan>(da.size());
        }
        if (!merge_into(merged, map ? &merged_spans : nullptr, 1, *sa, 1, policy, map)) continue;
        // A rebuilt array is located where its values came from.
        const bool from_src = policy == MergePolicy::Overwrite && has_values(*sa, 1);
        const Array& at = from_src ? *sa : da;
        Ref<Array> rebuilt = Array::make_from(merged, at.file(), at.line());
        if (map) {
            const SourceSpan* ext = map->extent(da);
            map->record(rebuilt, ext ? *ext : SourceSpan{}, merged_spans);
        }
        dst[found] = Node(std::move(rebuilt));
        changed = true;
    }
    return changed;
}

}  // namespace detail

Ref<Array> merge(const Array& base, const Array& over, MergePolicy policy) {
    Vec<Node> nodes(base.begin(), base.end());
    if (!detail::merge_into(nodes, nullptr, 0, over, 0, policy, nullptr))
        return Ref<Array>(const_cast<Array*>(&base));
    return Array::make_from(nodes, base.file(), base.line());
}

}  // namespace moira
