#include <moira/source_map.hpp>

namespace moira {

const std::vector<SourceSpan>* SourceMap::spans(const Array& a) const {
    auto it = arrays_.find(&a);
    return it == arrays_.end() || it->second.spans.size() != a.size() ? nullptr : &it->second.spans;
}

const SourceSpan* SourceMap::span(const Array& a, size_t i) const {
    const auto* s = spans(a);
    return s && i < s->size() ? &(*s)[i] : nullptr;
}

const SourceSpan* SourceMap::extent(const Array& a) const {
    auto it = arrays_.find(&a);
    return it == arrays_.end() ? nullptr : &it->second.extent;
}

const std::string* SourceMap::text(Symbol file) const {
    auto it = texts_.find(file);
    return it == texts_.end() ? nullptr : &it->second;
}

void SourceMap::record(const Ref<Array>& a, const SourceSpan& extent,
                       std::vector<SourceSpan> spans) {
    arrays_[a.get()] = Entry{a, extent, std::move(spans)};
}

void SourceMap::record_text(Symbol file, std::string_view text) {
    texts_.emplace(file, std::string(text));
}

}  // namespace moira
