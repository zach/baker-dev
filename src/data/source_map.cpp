#include <moira/source_map.hpp>

#include "common/containers.hpp"

namespace moira {

struct SourceMap::Tables {
    struct Entry {
        Ref<Array> keep;  // keeps the address from being reused while it is a key
        SourceSpan extent;
        Vec<SourceSpan> spans;
    };
    HashMap<const Array*, Entry> arrays;
    HashMap<Symbol, Str> texts;
};

// From the default allocator: a map has no owner, and snapshots share it (D61).
SourceMap::SourceMap() : t_(detail::create<Tables>(default_allocator())) {}
SourceMap::~SourceMap() {
    detail::destroy(default_allocator(), t_);
}

std::optional<std::span<const SourceSpan>> SourceMap::spans(const Array& a) const {
    auto it = t_->arrays.find(&a);
    if (it == t_->arrays.end() || it->second.spans.size() != a.size()) return std::nullopt;
    return std::span<const SourceSpan>(it->second.spans);
}

const SourceSpan* SourceMap::span(const Array& a, size_t i) const {
    const auto s = spans(a);
    return s && i < s->size() ? &(*s)[i] : nullptr;
}

const SourceSpan* SourceMap::extent(const Array& a) const {
    auto it = t_->arrays.find(&a);
    return it == t_->arrays.end() ? nullptr : &it->second.extent;
}

std::optional<std::string_view> SourceMap::text(Symbol file) const {
    auto it = t_->texts.find(file);
    if (it == t_->texts.end()) return std::nullopt;
    return std::string_view(it->second);
}

bool SourceMap::empty() const noexcept {
    return t_->arrays.empty();
}

void SourceMap::record(const Ref<Array>& a, const SourceSpan& extent,
                       std::span<const SourceSpan> spans) {
    t_->arrays[a.get()] =
        Tables::Entry{a, extent, Vec<SourceSpan>(spans.data(), spans.data() + spans.size())};
}

void SourceMap::record_text(Symbol file, std::string_view text) {
    t_->texts.emplace(file, Str(text));
}

}  // namespace moira
