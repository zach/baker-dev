// Config::check_layer (D161): a candidate layer's effect, the policy's refusals, and the
// validators' warnings, before it takes effect.

#include "config_impl.hpp"

#include <algorithm>

namespace baker {
namespace {

// Tag lookups for one check, through an index of each wide array met, built when it is
// first searched. A check resolves a few paths per changed value; scanning a wide table
// for each was most of its cost (D161, D164). It finds what Array::try_find finds: the
// first child array with the tag.
class Finder {
public:
    explicit Finder(const Allocator& a) : wide_(a), keys_(a), found_(a) {}

    [[nodiscard]] const Array* find(const Array& parent, Symbol tag) {
        if (parent.size() < kWide || tag.empty()) return parent.try_find(tag);
        const Wide& w = index(parent);
        for (uint32_t i = static_cast<uint32_t>(tag.hash()) & w.mask;; i = (i + 1) & w.mask) {
            const void* k = keys_[w.first + i];
            if (!k) return nullptr;
            if (k == tag.id()) return found_[w.first + i];
        }
    }
    // detail::resolve, through find().
    [[nodiscard]] bool resolve(const Array& root, const Path& path, detail::Resolved& out) {
        if (!path.valid()) return false;
        Values v(root.nodes(), &root);
        const Array* arr = &root;
        for (size_t i = 0; i < path.size(); ++i) {
            const Path::Segment& seg = path[i];
            if (seg.is_index) {
                if (seg.index >= v.size()) return false;
                if (const Array* a = v[seg.index].try_array()) {
                    v = Values(a->nodes(), a);
                    arr = a;
                } else {
                    v = Values(v.nodes().subspan(seg.index, 1), v.source());
                    arr = nullptr;
                }
                continue;
            }
            const Array* a = arr ? find(*arr, seg.name) : nullptr;
            if (!a) return false;
            v = a->values();
            arr = a;
        }
        out = {v, arr};
        return true;
    }
    // value_nodes, through find().
    bool values(const Array& root, const Path& path, Vec<Node>& out) {
        out.clear();
        detail::Resolved r;
        if (last_is_index(path)) {
            if (!resolve(root, parent_of(path), r)) return false;
            const uint32_t i = path[path.size() - 1].index;
            if (i >= r.values.size()) return false;
            out.push_back(r.values[i]);
            return true;
        }
        if (!resolve(root, path, r) || !r.array) return false;
        for (size_t i = path.empty() ? 0 : 1; i < r.array->size(); ++i)
            if (!is_tagged((*r.array)[i])) out.push_back((*r.array)[i]);
        return true;
    }

private:
    static constexpr size_t kWide = 32;  // below this, the tag run's scan is as quick
    struct Wide {
        const Array* array;
        uint32_t first, mask;  // its slots in keys_ and found_
    };

    const Wide& index(const Array& parent) {
        for (const Wide& w : wide_)
            if (w.array == &parent) return w;
        uint32_t cap = 64;
        while (cap < parent.size() * 2) cap *= 2;
        const Wide w{&parent, static_cast<uint32_t>(keys_.size()), cap - 1};
        keys_.resize(keys_.size() + cap, nullptr);
        found_.resize(found_.size() + cap, nullptr);
        for (const Node& n : parent.nodes()) {
            const Array* child = n.try_array();
            const Symbol tag = child ? child->tag() : Symbol();
            if (tag.empty()) continue;
            uint32_t i = static_cast<uint32_t>(tag.hash()) & w.mask;
            while (keys_[w.first + i] && keys_[w.first + i] != tag.id()) i = (i + 1) & w.mask;
            if (keys_[w.first + i]) continue;  // the first child with the tag wins
            keys_[w.first + i] = tag.id();
            found_[w.first + i] = child;
        }
        wide_.push_back(w);
        return wide_.back();
    }

    Vec<Wide> wide_;
    Vec<const void*> keys_;
    Vec<const Array*> found_;
};

bool is_code(const Node& n) noexcept {
    return n.type() == Type::Command || n.type() == Type::Property || n.type() == Type::Var;
}

bool is_scalar(const Node& n) noexcept {
    const Type t = n.type();
    return t == Type::Int || t == Type::Float || t == Type::Symbol || t == Type::String;
}

// data_only: every command, property, or variable in `a`, located at the array holding it.
// `tags` are the tags down to `a`, made a path only for a refusal.
void find_code(const Array& a, Vec<Symbol>& tags, Vec<Error>& out) {
    for (size_t i = 0; i < a.size(); ++i) {
        const Node& n = a[i];
        if (is_code(n)) {
            Str path;
            for (const Symbol t : tags) {
                if (!path.empty()) path += '.';
                detail::append_path_name(path, t.str());
            }
            const Array* at = n.array() ? n.array() : &a;
            out.push_back(refused(
                "'" + path + "' holds " + detail::preview(n) + "; the layer may hold data only",
                at->file(), at->line()));
        } else if (const Array* child = n.try_array()) {
            const Symbol tag = child->tag();
            if (!tag.empty()) tags.push_back(tag);
            find_code(*child, tags, out);
            if (!tag.empty()) tags.pop_back();
        }
    }
}

bool under(const Path& path, const Path& prefix) noexcept {
    if (prefix.size() > path.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (!(path[i] == prefix[i])) return false;
    return true;
}

}  // namespace

LayerReport Config::Impl::check_candidate(const Config& c, std::string_view name, const Array& tree,
                                          std::span<const Node> commands, const LayerPolicy& policy,
                                          std::span<const Error> read_warnings) const {
    Vec<LayerEffectEntry> effect;
    Vec<Error> refusals;
    Vec<Error> warnings(read_warnings.data(), read_warnings.data() + read_warnings.size());
    const Symbol layer(name);

    if (policy.data_only) {
        Vec<Symbol> tags(alloc);
        find_code(tree, tags, refusals);
        for (const Node& n : commands) {
            const Array* at = n.array();
            refusals.push_back(refused(
                "#autorun " + detail::preview(n) + " runs a command; the layer may hold data only",
                at ? at->file() : Symbol(), at ? at->line() : 0));
        }
    }
    const Array& stack = *c.root();
    Finder finder(alloc);
    // supplier() and source_in(), through the finder: the enabled layer, from the top, whose
    // own tree holds the value, and where.
    auto below_source = [&](const Path& path) -> std::optional<Source> {
        const bool index = last_is_index(path);
        for (size_t i = layers.size(); i-- > 0;) {
            if (!layers[i].options.enabled) continue;
            detail::Resolved r;
            if (!finder.resolve(*layers[i].root, path, r)) continue;
            if (index || (r.array && has_values(*r.array)))
                return located(layers[i], *r.values.source());
        }
        return std::nullopt;
    };
    Vec<Node> below;
    for (const Leaf& leaf : leaves_of(tree)) {
        const Path path(leaf.path);
        const bool removal = detail::is_removal(*leaf.holder);
        const bool exists = finder.values(stack, path, below);
        LayerEffectEntry e{};
        e.path = Symbol(leaf.path);
        e.value = pack(leaf.values);
        e.source = Source{layer, leaf.holder->file(), leaf.holder->line()};
        if (exists) {
            e.below = pack(below);
            e.below_source = below_source(path);
        }
        effect.push_back(std::move(e));

        auto refuse = [&](const Str& why) {
            refusals.push_back(
                refused("'" + leaf.path + "' " + why, leaf.holder->file(), leaf.holder->line()));
        };
        if (policy.scalars_only) {
            if (removal)
                refuse("is removed; the layer may only change values");
            else
                for (const Node& v : leaf.values)
                    if (!is_scalar(v)) {
                        refuse("is " + detail::preview(v) +
                               ", not a number, symbol, or string; the layer may only change "
                               "values to those");
                        break;
                    }
        }
        if (policy.existing_only) {
            if (removal)
                refuse("is removed; the layer may only change values the stack has");
            else if (!exists)
                refuse("is new; the layer may only change values the stack has");
        }
        if (!policy.allow.empty() && std::none_of(policy.allow.begin(), policy.allow.end(),
                                                  [&](const Path& p) { return under(path, p); }))
            refuse("is outside the paths the layer may change");
    }

    // The validators, over the stack with the candidate on top.
    const Ref<Array> merged = detail::merge(stack, tree, MergePolicy::Overwrite, merge_scratch);
    for (const Check& v : validators)
        v.fn(*merged, [&warnings](const Error& e) { warnings.push_back(e); });

    if (policy.warnings_refuse)
        for (const Error& w : warnings)
            refusals.push_back(
                refused("a warning refuses the layer: " + Str(w.message()), w.file(), w.line()));
    return LayerReport{List<LayerEffectEntry>(effect), List<Error>(refusals), List<Error>(warnings),
                       CheckStamp{}};
}

namespace config_internal {

namespace {
uint64_t mix(uint64_t h, uint64_t v) noexcept {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}
}  // namespace

uint64_t text_subject(std::string_view name, std::string_view text) noexcept {
    return mix(mix(1, detail::content_hash(name)), detail::content_hash(text));
}

uint64_t tree_subject(std::string_view name, const Array* tree) noexcept {
    return mix(mix(2, detail::content_hash(name)), reinterpret_cast<uintptr_t>(tree));
}

uint64_t policy_hash(const LayerPolicy& p) noexcept {
    uint64_t h = mix(3, (p.data_only ? 1u : 0u) | (p.scalars_only ? 2u : 0u) |
                            (p.existing_only ? 4u : 0u) | (p.warnings_refuse ? 8u : 0u));
    for (const Path& path : p.allow) {
        h = mix(h, path.size());
        for (size_t i = 0; i < path.size(); ++i)
            h = mix(mix(h, reinterpret_cast<uintptr_t>(path[i].name.id())),
                    path[i].is_index ? path[i].index + 1 : 0);
    }
    return h;
}

}  // namespace config_internal

void Config::stamp(LayerReport& report, uint64_t subject,
                   const LayerPolicy& policy) const noexcept {
    report.stamp.config_ = this;
    report.stamp.generation_ = impl_->generation;
    report.stamp.subject_ = subject;
    report.stamp.policy_ = policy_hash(policy);
}

bool Config::holds(const LayerOptions& options, uint64_t subject, bool on_top) const noexcept {
    const LayerReport* r = options.checked;
    return r && options.policy && on_top && r->stamp.config_ == this &&
           r->stamp.generation_ == impl_->generation && r->stamp.subject_ == subject &&
           r->stamp.policy_ == policy_hash(*options.policy);
}

Expected<LayerReport> Config::check_layer(std::string_view name, const Ref<Array>& root,
                                          const LayerPolicy& policy) const {
    if (!root) return Unexpected(make_error(ErrorCode::NotFound, "null layer to check"));
    LayerReport report = impl_->check_candidate(*this, name, *root, {}, policy, {});
    stamp(report, tree_subject(name, root.get()), policy);
    return report;
}

Expected<LayerReport> Config::check_layer_text(std::string_view name, std::string_view text,
                                               const LayerPolicy& policy) const {
    // The stack's session, so the candidate's packs meet the stack's; text only and reaching
    // no other file, as a layer from outside must be (D142), and as push_layer_text reads.
    LoadSession session = impl_->as_read;
    session.text_only = session.refuse_includes = true;
    session.clear_autorun();
    const size_t known = session.warnings().size();
    auto tree = read_string(text, name, session);
    if (!tree) return Unexpected(std::move(tree).error());
    const auto all = session.warnings();
    LayerReport report = impl_->check_candidate(*this, name, **tree, session.autorun(), policy,
                                                all.subspan(std::min(known, all.size())));
    stamp(report, text_subject(name, text), policy);
    return report;
}

}  // namespace baker
