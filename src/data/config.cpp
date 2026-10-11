#include <moira/config.hpp>
#include <moira/write.hpp>

#include <algorithm>
#include <cstdio>
#include <optional>

namespace moira {
namespace {

// A file a layer was built from (or looked for), as it was when the layer was read.
struct Dep {
    std::string path;
    bool present;
    uint64_t hash;
};

uint64_t hash_text(std::string_view s) noexcept {
    return detail::hash_symbol_text(s);
}

bool write_disk_file(const std::string& path, std::string_view text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

Error refused(std::string message, Symbol file = {}, int32_t line = 0) {
    return make_error(ErrorCode::Refused, std::move(message), file, line);
}

bool is_tagged(const Node& n) noexcept {
    const Array* a = n.try_array();
    return a && !a->empty() && (*a)[0].type() == Type::Symbol;
}

bool has_values(const Array& a) noexcept {
    for (size_t i = 1; i < a.size(); ++i)
        if (!is_tagged(a[i])) return true;
    return false;
}

int32_t line_at(const std::string& text, uint32_t offset) {
    const auto end = text.begin() + std::min<size_t>(offset, text.size());
    return 1 + static_cast<int32_t>(std::count(text.begin(), end, '\n'));
}

std::string quoted(const Path& path) {
    return "'" + path.to_string() + "'";
}

// The nodes a path's value consists of: the values of the array it names, or the one node
// an index selects. False if the path does not resolve.
bool value_nodes(const Array& root, const Path& path, std::vector<Node>& out) {
    out.clear();
    if (!path.empty() && path[path.size() - 1].is_index) {
        detail::Resolved r;
        if (detail::resolve(root, Path(path.to_string(path.size() - 1)), r) !=
            detail::ResolveStatus::Ok)
            return false;
        const uint32_t i = path[path.size() - 1].index;
        if (i >= r.values.size()) return false;
        out.push_back(r.values[i]);
        return true;
    }
    detail::Resolved r;
    if (detail::resolve(root, path, r) != detail::ResolveStatus::Ok || !r.array) return false;
    for (size_t i = path.empty() ? 0 : 1; i < r.array->size(); ++i)
        if (!is_tagged((*r.array)[i])) out.push_back((*r.array)[i]);
    return true;
}

bool same_nodes(std::span<const Node> a, std::span<const Node> b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const Node& x, const Node& y) { return structurally_equal(x, y); });
}

// Whether the subtree at `group` differs between two merged trees.
bool group_changed(const Array& before, const Array& after, const Path& group) {
    detail::Resolved a, b;
    const bool ha = detail::resolve(before, group, a) == detail::ResolveStatus::Ok;
    const bool hb = detail::resolve(after, group, b) == detail::ResolveStatus::Ok;
    if (ha != hb) return true;
    if (!ha) return false;
    if (a.array && b.array) return !structurally_equal(*a.array, *b.array);
    return !same_nodes(a.values.nodes(), b.values.nodes());
}

}  // namespace

struct Config::Impl {
    struct Layer {
        std::string name;
        std::string path;  // empty for an overlay
        Ref<Array> root;
        std::unique_ptr<SourceMap> map;
        std::vector<Dep> deps;
        std::shared_ptr<const LoadSession> after;  // macros and defines after this layer
    };
    struct Live {
        std::string key;  // the path's text
        Path path;
        std::vector<Node> values;
    };
    struct Subscriber {
        ReloadId id;
        Path group;
        std::function<void()> rebuild;
        std::function<Reload()> migrate;
    };

    ConfigOptions options;
    LoadSession base;
    Writer writer = write_disk_file;
    std::function<void(const Error&)> warn;
    std::vector<Layer> layers;
    std::vector<Live> live;
    Ref<Array> live_root;
    std::vector<Subscriber> subscribers;
    ReloadId next_id = 1;

    std::shared_ptr<const LoadSession> session_below(const std::vector<Layer>& ls, size_t i) const {
        return i == 0 ? std::make_shared<LoadSession>(base) : ls[i - 1].after;
    }

    // Reads a file layer, recording every file it reads (or looks for) for reload.
    Expected<void> read(Layer& layer, const LoadSession& before) const {
        auto session = std::make_shared<LoadSession>(before);
        session->autorun.clear();
        std::vector<Dep> deps;
        const LoadSession::Reader reader = before.reader();
        session->set_reader([&deps, &reader](const std::string& p, std::string& out) {
            const bool ok = reader(p, out);
            deps.push_back({p, ok, ok ? hash_text(out) : 0});
            return ok;
        });
        auto map = options.edit_mode ? std::make_unique<SourceMap>() : nullptr;
        session->source_map = map.get();
        auto root = try_read_file(layer.path, *session);
        session->set_reader(reader);
        session->source_map = nullptr;
        if (!root) return Unexpected(std::move(root).error());
        layer.root = std::move(*root);
        layer.map = std::move(map);
        layer.deps = std::move(deps);
        layer.after = std::move(session);
        return {};
    }

    bool changed(const Layer& layer) const {
        for (const Dep& d : layer.deps) {
            std::string text;
            const bool ok = base.reader()(d.path, text);
            if (ok != d.present || (ok && hash_text(text) != d.hash)) return true;
        }
        return false;
    }

    // Re-reads `ls[from..]` in order (each sees the macros of the ones below), all or
    // nothing.
    Expected<void> reread(std::vector<Layer>& ls, size_t from) const {
        std::vector<Layer> fresh;
        for (size_t i = from; i < ls.size(); ++i) {
            Layer l{ls[i].name, ls[i].path, ls[i].root, nullptr, {}, nullptr};
            const auto before = fresh.empty() ? session_below(ls, from) : fresh.back().after;
            if (l.path.empty()) {
                l.after = before;
            } else if (auto r = read(l, *before); !r) {
                return r;
            }
            fresh.push_back(std::move(l));
        }
        for (size_t i = from; i < ls.size(); ++i) ls[i] = std::move(fresh[i - from]);
        return {};
    }

    Ref<Array> merged() {
        Ref<Array> r = Array::make(0);
        for (const Layer& l : layers) r = moira::merge(*r, *l.root);
        live_root = nullptr;
        if (!live.empty()) {
            Ref<Array> overlay = Array::make(0);
            for (const Live& e : live) {
                // (a (b (c values...))) for the path a.b.c
                std::vector<Node> inner{Node(e.path[e.path.size() - 1].name)};
                inner.insert(inner.end(), e.values.begin(), e.values.end());
                Ref<Array> nest = Array::make_from(inner);
                for (size_t i = e.path.size() - 1; i-- > 0;)
                    nest = array(e.path[i].name, Node(nest));
                overlay = moira::merge(*overlay, *array(Node(nest)));
            }
            live_root = overlay;
            r = moira::merge(*r, *overlay);
        }
        return r;
    }

    // Installs a new merged tree: rebinds tweaks, then fires the reload callbacks.
    void commit(Config& c) {
        const Ref<Array> before = std::move(c.root_);
        c.root_ = merged();
        ++c.epoch_;
        std::vector<TweakBase*> moved;
        if (options.tweaks)
            options.tweaks->each([&](TweakBase& t) {
                if (t.bind(*c.root_, warn)) moved.push_back(&t);
            });
        for (TweakBase* t : moved) t->notify();
        const auto subs = subscribers;  // callbacks may subscribe or unsubscribe
        for (const Subscriber& s : subs) {
            if (!group_changed(*before, *c.root_, s.group)) continue;
            const Reload outcome = s.migrate ? s.migrate() : Reload::Punt;
            if (outcome == Reload::Punt && s.rebuild) s.rebuild();
        }
    }

    Expected<void> may_change(const Config& c) const {
        if (c.frozen_ && !options.live_layers)
            return Unexpected(
                refused("the config is frozen; layers and values can no longer change"));
        return {};
    }

    // The layer that supplies `path`'s value: the topmost whose own tree has the value
    // (overwrite merging means its values won). Searches below `top`.
    std::optional<size_t> supplier(const Path& path, size_t top) const {
        const bool index = !path.empty() && path[path.size() - 1].is_index;
        for (size_t i = top; i-- > 0;) {
            detail::Resolved r;
            if (detail::resolve(*layers[i].root, path, r) != detail::ResolveStatus::Ok) continue;
            if (index || (r.array && has_values(*r.array))) return i;
        }
        return std::nullopt;
    }

    Expected<void> write_to(Config& c, size_t li, const Path& path, const std::vector<Node>& values,
                            bool insert);
};

// --- Construction ---

Config::Config(ConfigOptions options) : root_(Array::make(0)), impl_(std::make_unique<Impl>()) {
    impl_->options = options;
    if (TweakRegistry* reg = options.tweaks) {
        reg->config_ = this;
        impl_->commit(*this);  // binds the registered tweaks (to nothing yet)
    }
}

Config::~Config() {
    if (TweakRegistry* reg = impl_->options.tweaks; reg && reg->config_ == this)
        reg->config_ = nullptr;
}

LoadSession& Config::session() noexcept {
    return impl_->base;
}

void Config::set_writer(Writer writer) {
    impl_->writer = writer ? std::move(writer) : Writer(write_disk_file);
}

void Config::set_warning_handler(std::function<void(const Error&)> handler) {
    impl_->warn = std::move(handler);
}

void Config::bind(TweakBase& t) {
    if (t.bind(*root_, impl_->warn)) t.notify();
}

// --- Layers ---

Expected<void> Config::try_push_layer(std::string_view name, std::string_view path) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    for (const auto& l : impl_->layers)
        if (l.name == name)
            return Unexpected(
                refused("there is already a layer named '" + std::string(name) + "'"));
    Impl::Layer layer{std::string(name), std::string(path), nullptr, nullptr, {}, nullptr};
    if (auto r = impl_->read(layer, *impl_->session_below(impl_->layers, impl_->layers.size())); !r)
        return r;
    impl_->layers.push_back(std::move(layer));
    impl_->commit(*this);
    return {};
}

Expected<void> Config::try_push_layer(std::string_view name, Ref<Array> overlay) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (!overlay)
        return Unexpected(
            make_error(ErrorCode::NotFound, "null overlay for layer '" + std::string(name) + "'"));
    for (const auto& l : impl_->layers)
        if (l.name == name)
            return Unexpected(
                refused("there is already a layer named '" + std::string(name) + "'"));
    auto after = impl_->session_below(impl_->layers, impl_->layers.size());
    impl_->layers.push_back(
        {std::string(name), {}, std::move(overlay), nullptr, {}, std::move(after)});
    impl_->commit(*this);
    return {};
}

Expected<void> Config::try_remove_layer(std::string_view name) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    auto& ls = impl_->layers;
    const auto it =
        std::find_if(ls.begin(), ls.end(), [&](const auto& l) { return l.name == name; });
    if (it == ls.end())
        return Unexpected(
            make_error(ErrorCode::NotFound, "no layer named '" + std::string(name) + "'"));
    const auto at = static_cast<size_t>(it - ls.begin());
    // The layers above were read with this one's macros; read them again without.
    std::vector<Impl::Layer> rest;
    for (size_t i = 0; i < ls.size(); ++i)
        if (i != at)
            rest.push_back({ls[i].name, ls[i].path, ls[i].root, nullptr, ls[i].deps, ls[i].after});
    if (auto r = impl_->reread(rest, at); !r) return r;
    for (size_t i = 0; i < at; ++i) rest[i].map = std::move(ls[i].map);
    ls = std::move(rest);
    impl_->commit(*this);
    return {};
}

Expected<void> Config::try_pop_layer() {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (impl_->layers.empty())
        return Unexpected(make_error(ErrorCode::NotFound, "no layer to pop"));
    impl_->layers.pop_back();
    impl_->commit(*this);
    return {};
}

std::vector<std::string> Config::layers() const {
    std::vector<std::string> names;
    for (const auto& l : impl_->layers) names.push_back(l.name);
    return names;
}

// --- Provenance ---

Expected<Source> Config::try_source(const Path& path) const {
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    if (path.empty()) return Unexpected(refused("the whole config has no single source"));
    detail::Resolved r;
    if (impl_->live_root &&
        detail::resolve(*impl_->live_root, path, r) == detail::ResolveStatus::Ok &&
        (!r.array || has_values(*r.array) || path[path.size() - 1].is_index))
        return Source{"live", {}, 0};
    std::optional<size_t> at = impl_->supplier(path, impl_->layers.size());
    if (!at) {
        // No layer has values there; name the topmost that has the path (a container).
        for (size_t i = impl_->layers.size(); i-- > 0 && !at;)
            if (detail::resolve(*impl_->layers[i].root, path, r) == detail::ResolveStatus::Ok)
                at = i;
    }
    if (!at) return Unexpected(detail::get_error(*root_, path, nullptr));
    const auto& layer = impl_->layers[*at];
    (void)detail::resolve(*layer.root, path, r);
    const Array& holder = *r.values.source();
    return Source{layer.name, holder.file(), holder.line()};
}

// --- Reload ---

Expected<bool> Config::try_reload() {
    if (kShipping || (frozen_ && !impl_->options.live_layers)) return false;
    auto& ls = impl_->layers;
    size_t from = ls.size();
    for (size_t i = 0; i < ls.size() && from == ls.size(); ++i)
        if (!ls[i].path.empty() && impl_->changed(ls[i])) from = i;
    if (from == ls.size()) return false;
    if (auto r = impl_->reread(ls, from); !r) return Unexpected(std::move(r).error());
    impl_->commit(*this);
    return true;
}

Config::ReloadId Config::on_reload(const Path& group, std::function<void()> rebuild,
                                   std::function<Reload()> migrate) {
    const ReloadId id = impl_->next_id++;
    impl_->subscribers.push_back({id, group, std::move(rebuild), std::move(migrate)});
    return id;
}

void Config::remove_on_reload(ReloadId id) {
    auto& s = impl_->subscribers;
    s.erase(std::remove_if(s.begin(), s.end(), [id](const auto& x) { return x.id == id; }),
            s.end());
}

// --- Live edits ---

Expected<void> Config::try_set(const Path& path, std::span<const Node> values) {
    if constexpr (kShipping)
        return Unexpected(refused("live edits are disabled under MOIRA_SHIPPING"));
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    if (path.empty()) return Unexpected(refused("cannot set the whole config"));
    for (size_t i = 0; i + 1 < path.size(); ++i)
        if (path[i].is_index)
            return Unexpected(
                refused("cannot set a value inside an indexed element: " + quoted(path)));
    Path target = path;
    std::vector<Node> vals(values.begin(), values.end());
    if (path[path.size() - 1].is_index) {
        // One element of a value: set the whole value with that element replaced.
        if (vals.size() != 1)
            return Unexpected(refused(quoted(path) + " is one element and takes one value"));
        target = Path(path.to_string(path.size() - 1));
        std::vector<Node> whole;
        if (!value_nodes(*root_, target, whole) || path[path.size() - 1].index >= whole.size())
            return Unexpected(detail::get_error(*root_, path, nullptr));
        whole[path[path.size() - 1].index] = vals[0];
        vals = std::move(whole);
    }
    const std::string key = target.to_string();
    auto& live = impl_->live;
    auto it = std::find_if(live.begin(), live.end(), [&](const auto& e) { return e.key == key; });
    if (it == live.end())
        live.push_back({key, target, std::move(vals)});
    else
        it->values = std::move(vals);
    impl_->commit(*this);
    return {};
}

std::vector<std::string> Config::unsaved() const {
    std::vector<std::string> keys;
    for (const auto& e : impl_->live) keys.push_back(e.key);
    return keys;
}

// --- Write-back ---

Expected<void> Config::try_write_back(const Path& path) {
    if constexpr (kShipping)
        return Unexpected(refused("write-back is disabled under MOIRA_SHIPPING"));
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    std::vector<Node> values;
    if (!value_nodes(*root_, path, values))
        return Unexpected(detail::get_error(*root_, path, nullptr));
    const auto at = impl_->supplier(path, impl_->layers.size());
    if (!at)
        return Unexpected(refused(
            quoted(path) + " is not in any layer's file; use write_back_to to add it to one"));
    return impl_->write_to(*this, *at, path, values, false);
}

Expected<void> Config::try_write_back_to(const Path& path, std::string_view layer) {
    if constexpr (kShipping)
        return Unexpected(refused("write-back is disabled under MOIRA_SHIPPING"));
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    std::vector<Node> values;
    if (!value_nodes(*root_, path, values))
        return Unexpected(detail::get_error(*root_, path, nullptr));
    const auto& ls = impl_->layers;
    const auto it =
        std::find_if(ls.begin(), ls.end(), [&](const auto& l) { return l.name == layer; });
    if (it == ls.end())
        return Unexpected(
            make_error(ErrorCode::NotFound, "no layer named '" + std::string(layer) + "'"));
    return impl_->write_to(*this, static_cast<size_t>(it - ls.begin()), path, values, true);
}

namespace {

std::string join(const std::vector<Node>& values) {
    std::string out;
    for (const Node& n : values) {
        if (!out.empty()) out += ' ';
        out += to_string(n);
    }
    return out;
}

struct Edit {
    uint32_t begin, end;
    std::string text;
};

}  // namespace

// Writes `values` as `path`'s value into layer `li`'s file, replacing only the tokens that
// held the old value (or, with `insert`, adding the key where it is missing), then
// re-reads that layer and the ones above, and drops the live edit the file now holds.
Expected<void> Config::Impl::write_to(Config& c, size_t li, const Path& path,
                                      const std::vector<Node>& values, bool insert) {
    const Layer& layer = layers[li];
    if (layer.path.empty())
        return Unexpected(refused(quoted(path) + " comes from layer '" + layer.name +
                                  "', which has no source file"));
    if (!layer.map)
        return Unexpected(refused("layer '" + layer.name +
                                  "' was not read in edit mode, so it cannot be written"));
    const SourceMap& map = *layer.map;

    // Walk the layer's own tree, checking that every step was written literally in a file.
    auto not_literal = [&](const Array& parent, size_t k) -> std::optional<Error> {
        const SourceSpan* sp = map.span(parent, k);
        if (!sp)
            return refused(
                quoted(path) + " is in an array assembled by #merge; edit it where it is defined",
                parent.file(), parent.line());
        if (!sp->macro.empty()) {
            const std::string* text = map.text(sp->file);
            return refused(quoted(path) + " comes from macro '" + std::string(sp->macro.str()) +
                               "'; edit the macro's definition instead",
                           sp->file, text ? line_at(*text, sp->begin) : 0);
        }
        if (sp->file.empty()) return refused(quoted(path) + " has no source text");
        return std::nullopt;
    };
    const Array* cur = layer.root.get();
    size_t offset = 0;  // where index 0 is: 1 after a tag, 0 in the root or an element
    std::vector<size_t> targets;
    size_t missing_from = path.size();
    for (size_t i = 0; i < path.size(); ++i) {
        const Path::Segment& seg = path[i];
        size_t k = cur->size();
        if (seg.is_index) {
            k = offset + seg.index < cur->size() ? offset + seg.index : cur->size();
        } else {
            for (size_t j = 0; j < cur->size() && k == cur->size(); ++j)
                if (is_tagged((*cur)[j]) && (*(*cur)[j].try_array())[0].is(seg.name)) k = j;
        }
        if (k == cur->size()) {
            missing_from = i;
            break;
        }
        if (auto e = not_literal(*cur, k)) return Unexpected(*e);
        const bool last = i + 1 == path.size();
        if (last && seg.is_index) {
            targets.push_back(k);
            break;
        }
        const Array* next = (*cur)[k].try_array();
        if (!next) return Unexpected(detail::get_error(*layer.root, path, nullptr));
        cur = next;
        offset = seg.is_index ? 0 : 1;
        if (last)
            for (size_t j = 1; j < cur->size(); ++j)
                if (!is_tagged((*cur)[j])) targets.push_back(j);
    }

    std::vector<Edit> edits;
    Symbol file;
    if (missing_from < path.size()) {
        // Add the key: (a (b value)) inside the deepest array that exists.
        if (!insert) return Unexpected(detail::get_error(*layer.root, path, nullptr));
        std::string text = join(values);
        for (size_t i = path.size(); i-- > missing_from;) {
            if (path[i].is_index)
                return Unexpected(refused("cannot add " + quoted(path) + " to layer '" +
                                          layer.name + "': it does not have the indexed element"));
            text = "(" + to_string(Node(path[i].name)) + (text.empty() ? "" : " ") + text + ")";
        }
        const SourceSpan* ext = map.extent(*cur);
        if (!ext || ext->file.empty() || (cur != layer.root.get() && ext->end == 0))
            return Unexpected(
                refused("cannot find where to add " + quoted(path), cur->file(), cur->line()));
        file = ext->file;
        if (cur == layer.root.get()) {
            const std::string* orig = map.text(file);
            const bool newline = orig && !orig->empty() && orig->back() != '\n';
            edits.push_back({ext->end, ext->end, (newline ? "\n" : "") + text + "\n"});
        } else {
            edits.push_back({ext->end - 1, ext->end - 1, " " + text});
        }
    } else {
        std::vector<Node> old;
        for (size_t k : targets) {
            if (auto e = not_literal(*cur, k)) return Unexpected(*e);  // e.g. (width kWidth)
            const SourceSpan& sp = *map.span(*cur, k);
            if (file.empty()) file = sp.file;
            if (sp.file != file)
                return Unexpected(refused(quoted(path) + " is written across several files",
                                          cur->file(), cur->line()));
            old.push_back((*cur)[k]);
        }
        if (targets.size() == values.size()) {
            for (size_t i = 0; i < targets.size(); ++i) {
                if (structurally_equal(old[i], values[i])) continue;
                const SourceSpan& sp = *map.span(*cur, targets[i]);
                edits.push_back({sp.begin, sp.end, to_string(values[i])});
            }
        } else if (targets.empty()) {
            if (auto e = not_literal(*cur, 0)) return Unexpected(*e);
            const SourceSpan* tag = map.span(*cur, 0);
            if (!tag)
                return Unexpected(refused("cannot find where to write " + quoted(path), cur->file(),
                                          cur->line()));
            file = tag->file;
            edits.push_back({tag->end, tag->end, " " + join(values)});
        } else {
            for (size_t i = 1; i < targets.size(); ++i)
                if (targets[i] != targets[i - 1] + 1)
                    return Unexpected(
                        refused("the values of " + quoted(path) +
                                    " are split by nested arrays, so their number cannot change",
                                cur->file(), cur->line()));
            edits.push_back({map.span(*cur, targets.front())->begin,
                             map.span(*cur, targets.back())->end, join(values)});
        }
    }

    if (!edits.empty()) {
        const std::string* orig = map.text(file);
        std::string disk;
        if (!orig || !base.reader()(std::string(file.str()), disk) || disk != *orig)
            return Unexpected(
                refused("'" + std::string(file.str()) +
                            "' has changed since it was read; reload before writing back",
                        file));
        std::sort(edits.begin(), edits.end(),
                  [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
        for (const Edit& e : edits) {
            if (e.begin > e.end || e.end > disk.size())
                return Unexpected(
                    refused("source spans do not match '" + std::string(file.str()) + "'", file));
            disk.replace(e.begin, e.end - e.begin, e.text);
        }
        if (!writer(std::string(file.str()), disk))
            return Unexpected(
                make_error(ErrorCode::Io, "cannot write '" + std::string(file.str()) + "'", file));
        if (auto r = reread(layers, li); !r) return r;
    }
    // The file holds the value now. Drop the live edit, unless a layer above still hides it.
    const std::string key =
        path[path.size() - 1].is_index ? path.to_string(path.size() - 1) : path.to_string();
    const auto it =
        std::find_if(live.begin(), live.end(), [&](const Live& e) { return e.key == key; });
    if (it != live.end()) {
        Live kept = std::move(*it);
        live.erase(it);
        std::vector<Node> now;
        const Ref<Array> without = merged();
        if (!value_nodes(*without, path, now) || !same_nodes(now, values))
            live.push_back(std::move(kept));
    }
    commit(c);
    return {};
}

#if MOIRA_EXCEPTIONS
Source Config::source(const Path& path) const {
    auto r = try_source(path);
    if (!r) detail::raise(std::move(r).error());
    return std::move(*r);
}

bool Config::reload() {
    auto r = try_reload();
    if (!r) detail::raise(std::move(r).error());
    return *r;
}
#endif

}  // namespace moira
