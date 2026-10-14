#include <baker/config.hpp>
#include <baker/tweak.hpp>
#include <baker/write.hpp>

#include "common/containers.hpp"

#include "text.hpp"

#include <optional>

namespace baker {
namespace {

constinit TweakRegistry g_registry;

// The metadata keys, interned once: a panel reads them for every tweak every frame.
struct Keys {
    Symbol range{"range"}, step{"step"}, desc{"desc"}, note{"note"}, kind{"kind"}, group{"group"};
};
const Keys& keys() {
    static const Keys k;
    return k;
}

// A tweak's value without the metadata written after it: (color 1 0.5 0 (range 0 1)) is
// three numbers (D88). Only trailing arrays tagged with the keys a tweak reads are left out,
// so a value whose own elements are tagged arrays reads as before.
Values without_metadata(const Values& v) {
    const Keys& k = keys();
    size_t n = v.size();
    for (; n > 0; --n) {
        const Array* a = v[n - 1].try_array();
        if (!a || a->empty()) break;
        const Node& tag = (*a)[0];
        if (!(tag.is(k.range) || tag.is(k.step) || tag.is(k.desc) || tag.is(k.note) ||
              tag.is(k.kind) || tag.is(k.group)))
            break;
    }
    return Values(v.nodes().first(n), v.source());
}

}  // namespace

// --- TweakRegistry ---

TweakRegistry& TweakRegistry::global() noexcept {
    return g_registry;
}

void TweakRegistry::lock() const noexcept {
    while (lock_.test_and_set(std::memory_order_acquire)) {
    }
}

void TweakRegistry::unlock() const noexcept {
    lock_.clear(std::memory_order_release);
}

size_t TweakRegistry::size() const noexcept {
    size_t n = 0;
    lock();
    for (const TweakBase* t = head_; t; t = t->next_) ++n;
    unlock();
    return n;
}

// --- TweakBase ---

struct TweakBase::Extra {
    std::optional<Path> parsed;
    Vec<Callback<void()>> on_change;
};

void TweakBase::attach() {
    registry_->lock();
    next_ = registry_->head_;
    if (next_) next_->prev_ = this;
    registry_->head_ = this;
    registry_->unlock();
    if (Config* c = registry_->config()) c->bind(*this);
}

void TweakBase::detach() noexcept {
    registry_->lock();
    if (prev_)
        prev_->next_ = next_;
    else
        registry_->head_ = next_;
    if (next_) next_->prev_ = prev_;
    prev_ = next_ = nullptr;
    registry_->unlock();
    detail::destroy(default_allocator(), extra_);
    extra_ = nullptr;
}

const Path& TweakBase::parsed() {
    if (!extra_) extra_ = detail::create<Extra>(default_allocator());
    if (!extra_->parsed) extra_->parsed.emplace(path_);
    return *extra_->parsed;
}

bool TweakBase::unbind() {
    if (!bound_) return false;
    bound_.reset();
    reset();
    return true;
}

bool TweakBase::bind(const Array& root, const Callback<void(const Error&)>& warn) {
    const Path& path = parsed();
    if constexpr (kShipping) {
        if (meta_.dev_only) return unbind();
    }
    detail::Resolved r;
    const auto status = detail::resolve(root, path, r);
    if (status != detail::ResolveStatus::Ok) {
        if (status == detail::ResolveStatus::Malformed && warn)
            warn(detail::get_error(root, path, nullptr));
        return unbind();
    }
    const Values values = without_metadata(r.values);
    const Array* holder = values.source();
    const auto first = static_cast<uint32_t>(values.begin() - holder->begin());
    const auto count = static_cast<uint32_t>(values.size());
    bool same = bound_ && count == count_;
    for (uint32_t i = 0; same && i < count; ++i)
        same = structurally_equal((*bound_)[first_ + i], values[i]);
    if (!same && !assign(values, true)) {
        if (warn) {
            const detail::ConvertInfo want = info();
            warn(detail::get_error(root, path, &want));
        }
        return unbind();
    }
    bound_ = Ref<Array>(const_cast<Array*>(holder));
    first_ = first;
    count_ = count;
    return !same;
}

void TweakBase::notify() {
    if (extra_)
        for (const auto& f : extra_->on_change) f();
}

void TweakBase::on_change(Callback<void()> callback) {
    if (!extra_) extra_ = detail::create<Extra>(default_allocator());
    extra_->on_change.push_back(std::move(callback));
}

Expected<void> TweakBase::set_values(std::span<const Node> values) {
    if constexpr (kShipping)
        return Unexpected(
            make_error(ErrorCode::Refused, "live edits are disabled under BAKER_SHIPPING"));
    const Values v(values, nullptr);
    if (!assign(v, false)) {
        Str text;
        for (const Node& n : values) text += (text.empty() ? "" : " ") + detail::preview(n);
        return Unexpected(make_error(ErrorCode::Type, "'" + Str(path_) + "' cannot be set to (" +
                                                          text + "), which is not " +
                                                          Str(info().name)));
    }
    if (Config* c = registry_->config()) return c->set(parsed(), values);
    (void)assign(v, true);
    notify();
    return {};
}

Expected<Source> TweakBase::source() const {
    Config* c = registry_->config();
    if (!c)
        return Unexpected(make_error(ErrorCode::NotFound, "'" + Str(path_) + "' has no config"));
    return c->source(const_cast<TweakBase*>(this)->parsed());
}

Expected<void> TweakBase::write_back() {
    Config* c = registry_->config();
    if (!c)
        return Unexpected(make_error(ErrorCode::NotFound, "'" + Str(path_) + "' has no config"));
    if (auto r = c->write_back(parsed()); !r) return Unexpected(std::move(r).error());
    return {};
}

// --- Metadata: the data file's, else the code's. ---

const Array* TweakBase::meta_array(Symbol key) const {
    return bound_ ? bound_->try_find(key) : nullptr;
}

TweakRange TweakBase::range() const {
    if (const Array* a = meta_array(keys().range); a && a->size() >= 3) {
        const auto lo = (*a)[1].try_float(), hi = (*a)[2].try_float();
        if (lo && hi) return {*lo, *hi};
    }
    return meta_.range;
}

double TweakBase::step() const {
    if (const Array* a = meta_array(keys().step); a && a->size() >= 2)
        if (auto v = (*a)[1].try_float()) return *v;
    return meta_.step;
}

namespace {
// A string or symbol value of `a`, or empty.
std::string_view text_value(const Array* a) {
    if (!a || a->size() < 2) return {};
    if (auto s = (*a)[1].try_str()) return *s;
    if (auto s = (*a)[1].try_sym()) return s->str();
    return {};
}
}  // namespace

std::string_view TweakBase::desc() const {
    const auto d = text_value(meta_array(keys().desc));
    return d.empty() ? meta_.desc : d;
}

std::string_view TweakBase::note() const {
    return text_value(meta_array(keys().note));
}

std::string_view TweakBase::kind() const {
    if (const auto k = text_value(meta_array(keys().kind)); !k.empty()) return k;
    if (!meta_.kind.empty()) return meta_.kind;
    // A 3- or 4-number value ranged 0..1 is presented as a color (after SeExpr's editor).
    if (bound_ && (count_ == 3 || count_ == 4)) {
        bool numbers = true;
        for (uint32_t i = 0; i < count_; ++i)
            numbers = numbers && (*bound_)[first_ + i].is_number();
        const TweakRange r = range();
        if (numbers && r.min == 0 && r.max == 1) return "color";
    }
    return {};
}

std::string_view TweakBase::group() const {
    if (const auto g = text_value(meta_array(keys().group)); !g.empty()) return g;
    if (!meta_.group.empty()) return meta_.group;
    const std::string_view p = path_;
    const size_t cut = p.find_last_of("./");
    return cut == std::string_view::npos ? std::string_view() : p.substr(0, cut);
}

}  // namespace baker
