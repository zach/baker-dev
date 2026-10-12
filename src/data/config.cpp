#include <moira/cache.hpp>
#include <moira/config.hpp>
#include <moira/write.hpp>

#include "common/containers.hpp"
#include "session.hpp"

#include "text.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <optional>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace moira {
namespace {

// Reads `path` through `reader` into `text`, replacing what it held.
bool read_into(const LoadSession::Reader& reader, std::string_view path, Str& text) {
    text.clear();
    return reader(path, detail::str_out(text));
}

// A file a layer was built from (or looked for), as it was when the layer was read.
struct Dep {
    Str path;
    bool present;
    uint64_t hash;
};

struct LayerState {
    Str name;
    Str path;  // as given; empty for an overlay
    Str file;  // the main file, normalized; empty for an overlay
    LayerOptions options;
    Ref<Array> root;
    Rc<SourceMap> map;  // edit mode; shared with snapshots, read-only once built
    Vec<Dep> deps;
    Rc<LoadSession> after;  // macros and defines after this layer; read-only
    bool missing = false;   // optional, and its file is absent
};

// One path in scratch. The array is owned by scratch alone, so set() may change it in
// place without allocating.
struct ScratchEntry {
    Str key;            // the path, '/'-separated
    Path path;          // names only
    Ref<Array> values;  // (last_name value...)
};

// A tagged array with values in some tree, found by walking tags (first occurrence of
// each tag, as find and merge use).
struct Leaf {
    Str path;  // dotted
    const Array* holder;
    Vec<Node> values;
};

uint64_t hash_text(std::string_view s) noexcept {
    return detail::hash_symbol_text(s);
}

Symbol trigger_name(const char* name) {
    return Symbol(name);
}

// Creates each missing parent directory of `path`. The library avoids std::filesystem
// (D22), so this is the one place it creates directories, for an optional layer's file.
void make_parents(const Str& path) {
    for (size_t at = path.find_first_of("/\\", 1); at != Str::npos;
         at = path.find_first_of("/\\", at + 1)) {
        const Str dir = path.substr(0, at);
#if defined(_WIN32)
        (void)_mkdir(dir.c_str());
#else
        (void)mkdir(dir.c_str(), 0777);
#endif
    }
}

bool write_disk_file(std::string_view where, std::string_view text) {
    const Str path(where);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        make_parents(path);
        f = std::fopen(path.c_str(), "wb");
    }
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

Error refused(Str message, Symbol file = {}, int32_t line = 0) {
    return make_error(ErrorCode::Refused, std::move(message), file, line);
}

[[maybe_unused]] Error shipping(std::string_view what) {
    return refused(Str(what) + " is disabled in a shipping build");
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

Vec<Node> values_of(const Array& a) {
    Vec<Node> out;
    for (size_t i = 1; i < a.size(); ++i)
        if (!is_tagged(a[i])) out.push_back(a[i]);
    return out;
}

// One node for a value: itself, or (v1 v2 ...) for several.
Node pack(const Vec<Node>& values) {
    if (values.size() == 1) return values[0];
    return Node(Array::make_copy(values));
}

int32_t line_at(std::string_view text, uint32_t offset) {
    const auto end = text.begin() + std::min<size_t>(offset, text.size());
    return 1 + static_cast<int32_t>(std::count(text.begin(), end, '\n'));
}

Str quoted(const Path& path) {
    return "'" + detail::text_of(path) + "'";
}

Path parent_of(const Path& path) {
    return Path(detail::text_of(path, path.size() - 1));
}

bool last_is_index(const Path& path) noexcept {
    return !path.empty() && path[path.size() - 1].is_index;
}

// Whether `path` is `base` plus one index segment.
bool is_element_of(const Path& path, const Path& base) noexcept {
    if (path.size() != base.size() + 1 || !last_is_index(path)) return false;
    for (size_t i = 0; i < base.size(); ++i)
        if (!(path[i] == base[i])) return false;
    return true;
}

// The nodes a path's value consists of: the values of the array it names, or the one node
// an index selects. False if the path does not resolve.
bool value_nodes(const Array& root, const Path& path, Vec<Node>& out) {
    out.clear();
    if (last_is_index(path)) {
        detail::Resolved r;
        if (detail::resolve(root, parent_of(path), r) != detail::ResolveStatus::Ok) return false;
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

void collect_leaves(const Array& a, size_t first, const Str& prefix, Vec<Leaf>& out) {
    Vec<Symbol> seen;
    for (size_t i = first; i < a.size(); ++i) {
        if (!is_tagged(a[i])) continue;
        const Array& child = *a[i].try_array();
        const Symbol tag = *child[0].try_sym();
        if (std::find(seen.begin(), seen.end(), tag) != seen.end()) continue;
        seen.push_back(tag);
        Str path = prefix;
        if (!path.empty()) path += '.';
        detail::append_path_name(path, tag.str());
        if (has_values(child)) out.push_back({path, &child, values_of(child)});
        collect_leaves(child, 1, path, out);
    }
}

Vec<Leaf> leaves_of(const Array& root) {
    Vec<Leaf> out;
    collect_leaves(root, 0, "", out);
    return out;
}

Str join(const Vec<Node>& values) {
    Str out;
    for (const Node& n : values) {
        if (!out.empty()) out += ' ';
        out += detail::text_of(n);
    }
    return out;
}

// path[from..] holding `values`, in the pretty-printer's form: (a (b value)). Lines after
// the first are indented by `indent`.
Str render_entry(const Path& path, size_t from, const Vec<Node>& values, const Str& indent) {
    Vec<Node> inner{Node(path[path.size() - 1].name)};
    inner.insert(inner.end(), values.begin(), values.end());
    Ref<Array> nest = Array::make_from(inner);
    for (size_t i = path.size() - 1; i-- > from;) nest = array(path[i].name, Node(nest));
    Str text = detail::dta_of(*array(Node(nest)));
    while (!text.empty() && text.back() == '\n') text.pop_back();
    for (size_t at = text.find('\n'); at != Str::npos; at = text.find('\n', at + 1))
        text.insert(at + 1, indent);
    return text;
}

Vec<ScratchEntry> copy_scratch(const Vec<ScratchEntry>& from) {
    Vec<ScratchEntry> out;
    for (const ScratchEntry& e : from)
        out.push_back({e.key, e.path, Array::make_copy(e.values->nodes())});
    return out;
}

struct Edit {
    uint32_t begin, end;
    Str text;
};

// A planned write: the new text of one file.
struct Plan {
    Symbol file;
    Str text;
    bool changed = false;
};

}  // namespace

struct ConfigSnapshot::State {
    explicit State(const Allocator& a) : alloc(a) {}
    const Allocator alloc;  // the Config's, which outlives its snapshots (D61)
    Vec<LayerState> layers{alloc};
    Vec<ScratchEntry> scratch{alloc};
    bool scratch_enabled = true;
    mutable std::atomic<uint32_t> refs{1};
};

ConfigSnapshot::ConfigSnapshot(const ConfigSnapshot& other) noexcept : state_(other.state_) {
    if (state_) state_->refs.fetch_add(1, std::memory_order_relaxed);
}
ConfigSnapshot& ConfigSnapshot::operator=(const ConfigSnapshot& other) noexcept {
    ConfigSnapshot copy(other);
    std::swap(state_, copy.state_);
    return *this;
}
ConfigSnapshot::~ConfigSnapshot() {
    if (state_ && state_->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
        detail::destroy(state_->alloc, state_);
}

struct Config::Impl {
    struct Subscriber {
        ReloadId id;
        Path group;
        Callback<void()> rebuild;
        Callback<Reload()> migrate;
    };
    struct Completion {
        ReloadId id;
        Callback<void(const ReloadRecord&)> fn;
    };

    explicit Impl(const Allocator& a) : alloc(a) {
        base.allocator = &alloc;  // every layer's tree is the Config's (D64)
    }

    const Allocator alloc;  // first: the containers below take it
    ConfigOptions options;
    Str cache_dir{alloc};  // options.cache_dir views this
    LoadSession base;
    Writer writer = write_disk_file;
    Callback<void(const Error&)> warn;
    Vec<LayerState> layers{alloc};
    Vec<ScratchEntry> scratch{alloc};
    bool scratch_enabled = true;
    Vec<Subscriber> subscribers{alloc};
    Vec<Completion> completions{alloc};
    ReloadId next_id = 1;

    TimeSource clock = steady_now;
    void* clock_user = nullptr;
    Vec<ReloadRecord> ring{alloc};  // reserved once, at its capacity
    size_t ring_cap = 256;
    size_t ring_next = 0;

    // --- Telemetry ---

    [[nodiscard]] uint64_t now() const {
        if constexpr (kShipping)
            return 0;
        else
            return clock(clock_user);
    }
    [[nodiscard]] ReloadRecord begin(Symbol trigger, uint64_t origin = 0) const {
        ReloadRecord r;
        r.trigger = trigger;
        r.origin_ns = origin;
        r.start_ns = now();
        return r;
    }
    void lap(uint64_t& field, uint64_t& t) const {
        const uint64_t n = now();
        field += n - t;
        t = n;
    }
    // Ends the record at `end` (the end of its last stage, so the stages tile the whole
    // change), keeps it, and hands it to the completion callbacks. No allocation once the
    // ring is reserved.
    void finish(const Config& c, ReloadRecord& r, uint64_t end) {
        if constexpr (!kShipping) {
            r.epoch = c.epoch_;
            r.end_ns = end;
            if (ring_cap) {
                if (ring.capacity() < ring_cap) ring.reserve(ring_cap);
                if (ring.size() < ring_cap)
                    ring.push_back(r);
                else
                    ring[ring_next] = r;
                ring_next = (ring_next + 1) % ring_cap;
            }
            for (size_t i = 0; i < completions.size(); ++i) completions[i].fn(r);
        }
    }

    // --- Reading layers ---

    Rc<LoadSession> session_below(const Vec<LayerState>& ls, size_t i) const {
        return i == 0 ? Rc<LoadSession>::make(alloc, base) : ls[i - 1].after;
    }

    // A fresh cache of `path`, or the text and a new cache, written through the writer: a
    // cache that cannot be written costs the next read a parse, not this one its tree.
    Expected<Ref<Array>> read_through_cache(std::string_view path, const Str& cache,
                                            LoadSession& session) const {
        if constexpr (kShipping) {  // shipping trusts the cache, and writes none
            if (auto r = read_cache(cache, session)) return r;
            return read_file(path, session);
        } else {
            if (Ref<Array> fresh = detail::read_fresh_cache(cache, session)) return fresh;
            session.record_for_cache = true;
            auto root = read_file(path, session);
            session.record_for_cache = false;
            Str bytes(alloc);
            if (root && write_cache(**root, detail::str_out(bytes), &session))
                (void)writer(cache, bytes);
            return root;
        }
    }

    // Reads a file layer, recording every file it reads (or looks for) for reload, and the
    // time spent reading them into `read_ns`.
    Expected<void> read(LayerState& layer, const LoadSession& before,
                        uint64_t* read_ns = nullptr) const {
        auto session = Rc<LoadSession>::make(alloc, before);
        session->clear_autorun();
        Vec<Dep> deps;
        const LoadSession::Reader reader = before.reader();
        // Edit mode reads text, which keeps the source maps write-back needs.
        Str cache(alloc);
        if (!options.edit_mode && !cache_dir.empty()) {
            // The cache's name: the hash of the layer's path, in hex.
            char name[17];
            uint64_t h = detail::content_hash(layer.path);
            for (int i = 15; i >= 0; --i, h >>= 4) name[i] = "0123456789abcdef"[h & 15];
            name[16] = '\0';
            cache.append(cache_dir).append("/").append(name).append(".datb");
        }
        session->set_reader([&](std::string_view p, TextSink out) {
            const uint64_t t0 = read_ns ? now() : 0;
            Str text;
            const bool ok = read_into(reader, p, text);
            if (read_ns) *read_ns += now() - t0;
            if (p != cache.view()) deps.push_back({Str(p), ok, ok ? hash_text(text) : 0});
            if (ok) out(text);
            return ok;
        });
        auto map = options.edit_mode ? Rc<SourceMap>::make(alloc) : nullptr;
        session->source_map = map.get();
        auto root = cache.empty() ? read_file(layer.path, *session)
                                  : read_through_cache(layer.path, cache, *session);
        session->set_reader(reader);
        session->source_map = nullptr;
        layer.file = deps.empty() ? layer.path : deps.front().path;
        layer.missing = false;
        if (!root) {
            // An optional layer whose file is absent is an empty layer.
            if (!layer.options.optional || deps.empty() || deps.front().present)
                return Unexpected(std::move(root).error());
            root = Array::make(0, Symbol(layer.file), 1);
            layer.missing = true;
        }
        layer.root = std::move(*root);
        layer.map = std::move(map);
        layer.deps = std::move(deps);
        layer.after = std::move(session);
        return {};
    }

    bool changed(const LayerState& layer, Symbol& first) const {
        for (const Dep& d : layer.deps) {
            Str text;
            const bool ok = read_into(base.reader(), d.path, text);
            if (ok != d.present || (ok && hash_text(text) != d.hash)) {
                first = Symbol(d.path);
                return true;
            }
        }
        return false;
    }

    // Re-reads `ls[from..]` in order (each sees the macros of the ones below), all or
    // nothing.
    Expected<void> reread(Vec<LayerState>& ls, size_t from, uint64_t* read_ns = nullptr,
                          uint32_t* files = nullptr) const {
        Vec<LayerState> fresh;
        for (size_t i = from; i < ls.size(); ++i) {
            LayerState l = ls[i];
            const auto before = fresh.empty() ? session_below(ls, from) : fresh.back().after;
            if (l.path.empty()) {
                l.after = before;
            } else {
                if (auto r = read(l, *before, read_ns); !r) return r;
                if (files)
                    for (const Dep& d : l.deps) *files += d.present ? 1u : 0u;
            }
            fresh.push_back(std::move(l));
        }
        for (size_t i = from; i < ls.size(); ++i) ls[i] = std::move(fresh[i - from]);
        return {};
    }

    std::optional<size_t> find_layer(std::string_view name) const {
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i].name == name) return i;
        return std::nullopt;
    }

    // --- Scratch ---

    // (a (b (c values...))) for every scratch path, or null when scratch is empty.
    [[nodiscard]] Ref<Array> scratch_root() const {
        if (scratch.empty()) return nullptr;
        Ref<Array> overlay = Array::make(0);
        for (const ScratchEntry& e : scratch) {
            Ref<Array> nest = Array::make_copy(e.values->nodes());
            for (size_t i = e.path.size() - 1; i-- > 0;) nest = array(e.path[i].name, Node(nest));
            overlay = moira::merge(*overlay, *array(Node(nest)));
        }
        return overlay;
    }

    // The scratch entry for `path`, or for the value `path` is an element of.
    const ScratchEntry* scratch_entry(const Path& path) const noexcept {
        if (!scratch_enabled) return nullptr;
        for (const ScratchEntry& e : scratch)
            if (e.path == path || is_element_of(path, e.path)) return &e;
        return nullptr;
    }

    // The current value at `path`: scratch first, then the merged tree.
    bool current(const Config& c, const Path& path, Vec<Node>& out) const {
        if (const ScratchEntry* e = scratch_entry(path)) {
            const Values v = e->values->values();
            out.clear();
            if (e->path == path) {
                out.assign(v.begin(), v.end());
                return true;
            }
            const uint32_t i = path[path.size() - 1].index;
            if (i >= v.size()) return false;
            out.push_back(v[i]);
            return true;
        }
        return value_nodes(*c.root_, path, out);
    }

    // Updates the tweaks bound to `e`'s path (or to one of its elements) from scratch,
    // without a merge, and fires only their on_change. Allocates nothing for up to 16
    // affected tweaks.
    void retune(const ScratchEntry& e) {
        if (!options.tweaks) return;
        TweakBase* few[16];
        size_t n = 0;
        // Lazily: even an empty vector allocates in MSVC's debug library.
        Own<Vec<TweakBase*>> more;
        const Values all = e.values->values();
        options.tweaks->each([&](TweakBase& t) {
            const Path& tp = t.parsed();
            Values v;
            uint32_t first = 1;
            if (tp == e.path) {
                v = all;
            } else if (is_element_of(tp, e.path) && tp[tp.size() - 1].index < all.size()) {
                const uint32_t i = tp[tp.size() - 1].index;
                v = Values(all.nodes().subspan(i, 1), e.values.get());
                first += i;
            } else {
                return;
            }
            if (!t.assign(v, true)) return;  // reported at the next full merge
            t.bound_ = e.values;
            t.first_ = first;
            t.count_ = static_cast<uint32_t>(v.size());
            if (n < 16) {
                few[n++] = &t;
            } else {
                if (!more) more = Own<Vec<TweakBase*>>::make(default_allocator());
                more->push_back(&t);
            }
        });
        for (size_t i = 0; i < n; ++i) few[i]->notify();
        if (more)
            for (TweakBase* t : *more) t->notify();
    }

    // Sets `target`'s value in scratch (a names-only path), in place when the entry exists
    // with as many values.
    void put(Config& c, const Path& target, std::span<const Node> values) {
        ScratchEntry* found = nullptr;
        for (ScratchEntry& e : scratch)
            if (e.path == target) found = &e;
        if (found && found->values->size() == values.size() + 1) {
            for (size_t i = 0; i < values.size(); ++i) found->values->set(i + 1, values[i]);
        } else {
            Vec<Node> nodes{Node(target[target.size() - 1].name)};
            nodes.insert(nodes.end(), values.data(), values.data() + values.size());
            Ref<Array> arr = Array::make_from(nodes);
            if (found)
                found->values = std::move(arr);
            else
                scratch.push_back({detail::text_of(target), target, std::move(arr)});
            found = found ? found : &scratch.back();
        }
        c.scratch_dirty_ = true;
        retune(*found);
    }

    // --- Merging ---

    [[nodiscard]] Ref<Array> merged() const {
        Ref<Array> r = Array::make(0);
        for (const LayerState& l : layers)
            if (l.options.enabled) r = moira::merge(*r, *l.root);
        if (scratch_enabled)
            if (Ref<Array> s = scratch_root()) r = moira::merge(*r, *s);
        return r;
    }

    // Installs a new merged tree, rebinds tweaks, then fires the reload callbacks, timing
    // the merge, bind, and notify stages from `t`.
    void commit(Config& c, ReloadRecord& rec, uint64_t& t) {
        const Ref<Array> before = std::move(c.root_);
        c.root_ = merged();
        c.scratch_dirty_ = false;
        lap(rec.merge_ns, t);
        ++c.epoch_;
        Vec<TweakBase*> moved;
        if (options.tweaks)
            options.tweaks->each([&](TweakBase& tw) {
                if (tw.bind(*c.root_, warn)) moved.push_back(&tw);
            });
        rec.tweaks_changed = static_cast<uint32_t>(moved.size());
        lap(rec.bind_ns, t);
        for (TweakBase* tw : moved) tw->notify();
        const auto subs = subscribers;  // callbacks may subscribe or unsubscribe
        for (const Subscriber& s : subs) {
            if (!group_changed(*before, *c.root_, s.group)) continue;
            const Reload outcome = s.migrate ? s.migrate() : Reload::Punt;
            if (outcome == Reload::Punt) {
                ++rec.punted;
                if (s.rebuild) s.rebuild();
            } else {
                ++rec.migrated;
            }
        }
        lap(rec.notify_ns, t);
    }

    // A change with no files to read (a layer change, a restore): merge, bind, notify,
    // record.
    void change(Config& c, Symbol trigger) {
        ReloadRecord rec = begin(trigger);
        uint64_t t = rec.start_ns;
        commit(c, rec, t);
        finish(c, rec, t);
    }

    Expected<void> may_change(const Config& c) const {
        if (c.frozen_ && !options.live_layers)
            return Unexpected(
                refused("the config is frozen; layers and values can no longer change"));
        return {};
    }

    // The enabled layer below `top` (skipping `skip`) whose own tree has `path`'s value:
    // overwrite merging means its values won.
    std::optional<size_t> supplier(const Path& path, size_t top, size_t skip = SIZE_MAX) const {
        const bool index = last_is_index(path);
        for (size_t i = top; i-- > 0;) {
            if (i == skip || !layers[i].options.enabled) continue;
            detail::Resolved r;
            if (detail::resolve(*layers[i].root, path, r) != detail::ResolveStatus::Ok) continue;
            if (index || (r.array && has_values(*r.array))) return i;
        }
        return std::nullopt;
    }

    [[nodiscard]] Source source_in(const LayerState& l, const Path& path) const {
        detail::Resolved r;
        if (detail::resolve(*l.root, path, r) != detail::ResolveStatus::Ok)
            return {Symbol(l.name), {}, 0};
        const Array& holder = *r.values.source();
        return {Symbol(l.name), holder.file(), holder.line()};
    }

    // --- Write-back ---

    Expected<Plan> plan(const LayerState& layer, const Path& path, const Vec<Node>& values,
                        bool insert, const LoadSession::Reader& read_file) const;
    Expected<WriteReport> write_into(Config& c, size_t li, const Path& path,
                                     const Vec<Node>& values, bool insert);
};

// Plans writing `values` as `path`'s value into `layer`'s file: replacing only the tokens
// that hold the old value or, with `insert`, adding the entry as the last child of the
// deepest ancestor written in that file (or creating an optional layer's absent file).
// `read_file` gives the file as it is now; it must match what the layer was read from.
Expected<Plan> Config::Impl::plan(const LayerState& layer, const Path& path,
                                  const Vec<Node>& values, bool insert,
                                  const LoadSession::Reader& read_file) const {
    if (layer.path.empty())
        return Unexpected(refused(quoted(path) + " comes from layer '" + layer.name +
                                  "', which has no source file"));
    if (!layer.map)
        return Unexpected(refused("layer '" + layer.name +
                                  "' was not read in edit mode, so it cannot be written"));
    const SourceMap& map = *layer.map;
    const Symbol main(layer.file);
    auto unchanged = [&](Symbol file) -> std::optional<Error> {
        const auto orig = map.text(file);
        Str disk;
        if (!orig || !read_into(read_file, file.str(), disk) || disk != *orig)
            return refused("'" + Str(file.str()) +
                               "' has changed since it was read; reload before writing back",
                           file);
        return std::nullopt;
    };
    auto no_index = [&](size_t from, Symbol file, int32_t line) -> std::optional<Error> {
        for (size_t i = from; i < path.size(); ++i)
            if (path[i].is_index)
                return refused(quoted(path) + " names element " + detail::decimal(path[i].index) +
                                   ", which layer '" + layer.name + "' does not have",
                               file, line);
        return std::nullopt;
    };

    if (layer.missing) {
        if (!insert) return Unexpected(detail::get_error(*layer.root, path, nullptr));
        if (auto e = no_index(0, main, 0)) return Unexpected(*e);
        Str disk;
        if (read_into(read_file, layer.file, disk))
            return Unexpected(refused(
                "'" + layer.file + "' has appeared since it was read; reload before writing back",
                main));
        return Plan{main, render_entry(path, 0, values, "") + "\n", true};
    }

    // Walk the layer's own tree, checking that every step was written literally in a file.
    auto not_literal = [&](const Array& parent, size_t k) -> std::optional<Error> {
        const SourceSpan* sp = map.span(parent, k);
        if (!sp)
            return refused(
                quoted(path) + " is in an array assembled by #merge; edit it where it is defined",
                parent.file(), parent.line());
        if (!sp->macro.empty()) {
            const auto text = map.text(sp->file);
            return refused(quoted(path) + " comes from macro '" + Str(sp->macro.str()) +
                               "'; edit the macro's definition instead",
                           sp->file, text ? line_at(*text, sp->begin) : 0);
        }
        if (sp->file.empty()) return refused(quoted(path) + " has no source text");
        return std::nullopt;
    };
    const Array* cur = layer.root.get();
    size_t offset = 0;  // where index 0 is: 1 after a tag, 0 in the root or an element
    Vec<size_t> targets;
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

    Vec<Edit> edits;
    Symbol file;
    if (missing_from < path.size()) {
        if (auto e = no_index(missing_from, cur->file(), cur->line())) return Unexpected(*e);
        if (!insert) return Unexpected(detail::get_error(*layer.root, path, nullptr));
        file = main;
        const auto text = map.text(main);
        if (!text) return Unexpected(refused("no source text for '" + layer.file + "'", main));
        if (cur == layer.root.get()) {
            // A new top-level form at the end of the file.
            const bool newline = !text->empty() && text->back() != '\n';
            const auto end = static_cast<uint32_t>(text->size());
            edits.push_back(
                {end, end,
                 (newline ? "\n" : "") + render_entry(path, missing_from, values, "") + "\n"});
        } else {
            // The last child of the deepest ancestor written in this file itself.
            const SourceSpan* ext = map.extent(*cur);
            if (!ext || ext->file != main)
                return Unexpected(
                    refused("cannot add " + quoted(path) + " to '" + layer.file +
                                "': the array it would go in comes from another file (#include or "
                                "#merge), not this one",
                            cur->file(), cur->line()));
            const SourceSpan* last = map.span(*cur, cur->size() - 1);
            if (!last || last->file != main || last->end > text->size() || ext->end == 0)
                return Unexpected(
                    refused("cannot find where to add " + quoted(path), cur->file(), cur->line()));
            const bool one_line = text->find('\n', ext->begin) == Str::npos ||
                                  text->find('\n', ext->begin) >= last->begin;
            if (one_line) {
                edits.push_back(
                    {last->end, last->end, " " + render_entry(path, missing_from, values, "")});
            } else {
                // Indented like the previous sibling, after the rest of its line.
                const size_t line_start = text->rfind('\n', last->begin) + 1;
                size_t ws = line_start;
                while (ws < last->begin && ((*text)[ws] == ' ' || (*text)[ws] == '\t')) ++ws;
                const Str indent(text->substr(line_start, ws - line_start));
                const size_t line_end = text->find('\n', last->end);
                const auto at = static_cast<uint32_t>(
                    line_end != Str::npos && line_end < ext->end - 1 ? line_end : last->end);
                edits.push_back(
                    {at, at, "\n" + indent + render_entry(path, missing_from, values, indent)});
            }
        }
    } else {
        Vec<Node> old;
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
                edits.push_back({sp.begin, sp.end, detail::text_of(values[i])});
            }
        } else if (targets.empty()) {
            if (auto e = not_literal(*cur, 0)) return Unexpected(*e);
            const SourceSpan* tag = map.span(*cur, 0);
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
    if (edits.empty()) return Plan{file, {}, false};

    if (auto e = unchanged(file)) return Unexpected(*e);
    Str text(*map.text(file));
    std::sort(edits.begin(), edits.end(),
              [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    for (const Edit& e : edits) {
        if (e.begin > e.end || e.end > text.size())
            return Unexpected(refused("source spans do not match '" + Str(file.str()) + "'", file));
        text.replace(e.begin, e.end - e.begin, e.text);
    }
    return Plan{file, std::move(text), true};
}

// Writes one value into layer `li`, re-reads that layer and those above (trigger
// write_back), and drops the scratch entry the file now holds.
Expected<WriteReport> Config::Impl::write_into(Config& c, size_t li, const Path& path,
                                               const Vec<Node>& values, bool insert) {
    auto p = plan(layers[li], path, values, insert, base.reader());
    if (!p) return Unexpected(std::move(p).error());
    ReloadRecord rec = begin(trigger_name("write_back"));
    uint64_t t = rec.start_ns;
    if (p->changed) {
        if (!writer(Str(p->file.str()), p->text))
            return Unexpected(
                make_error(ErrorCode::Io, "cannot write '" + Str(p->file.str()) + "'", p->file));
        rec.first_file = p->file;
        const uint64_t t0 = now();
        auto r = reread(layers, li, &rec.read_ns, &rec.files_reloaded);
        t = now();
        rec.parse_ns = t - t0 - rec.read_ns;
        if (!r) {
            rec.outcome = ReloadOutcome::ParseError;
            finish(c, rec, t);
            return Unexpected(std::move(r).error());
        }
    }
    // The file holds the value now. Drop it from scratch, unless a layer above still hides it.
    const Path key = last_is_index(path) ? parent_of(path) : path;
    const auto it = std::find_if(scratch.begin(), scratch.end(),
                                 [&](const ScratchEntry& e) { return e.path == key; });
    if (it != scratch.end()) {
        ScratchEntry kept = std::move(*it);
        scratch.erase(it);
        Vec<Node> now_values;
        if (!value_nodes(*merged(), path, now_values) || !same_nodes(now_values, values))
            scratch.push_back(std::move(kept));
    }
    commit(c, rec, t);
    finish(c, rec, t);
    WriteReport report;
    if (p->changed) {
        const Source s = source_in(layers[li], path);
        const WrittenLine line{s.file, s.line};
        report.written = List<WrittenLine>(std::span<const WrittenLine>(&line, 1));
    }
    return report;
}

// --- Construction ---

Config::Config(ConfigOptions options, const Allocator& allocator)
    : root_(Array::make(0)), impl_(detail::create<Impl>(allocator, allocator)) {
    impl_->options = options;
    impl_->cache_dir.assign(options.cache_dir);
    impl_->options.cache_dir = impl_->cache_dir;
    if (TweakRegistry* reg = options.tweaks) {
        reg->config_ = this;
        ReloadRecord unused;
        uint64_t t = 0;
        impl_->commit(*this, unused, t);  // binds the registered tweaks (to nothing yet)
    }
}

Config::~Config() {
    if (TweakRegistry* reg = impl_->options.tweaks; reg && reg->config_ == this)
        reg->config_ = nullptr;
    const Allocator alloc = impl_->alloc;
    detail::destroy(alloc, impl_);
}

LoadSession& Config::session() noexcept {
    return impl_->base;
}

void Config::set_writer(Writer writer) {
    impl_->writer = writer ? std::move(writer) : Writer(write_disk_file);
}

void Config::set_warning_handler(Callback<void(const Error&)> handler) {
    impl_->warn = std::move(handler);
}

void Config::bind(TweakBase& t) {
    if (t.bind(*root_, impl_->warn)) t.notify();
}

// --- Layers ---

Expected<void> Config::push_layer(std::string_view name, std::string_view path,
                                  LayerOptions options) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (name == "scratch" || impl_->find_layer(name))
        return Unexpected(refused("there is already a layer named '" + Str(name) + "'"));
    LayerState layer;
    layer.name = Str(name);
    layer.path = Str(path);
    layer.options = options;
    if (auto r = impl_->read(layer, *impl_->session_below(impl_->layers, impl_->layers.size())); !r)
        return r;
    impl_->layers.push_back(std::move(layer));
    impl_->change(*this, trigger_name("layer"));
    return {};
}

Expected<void> Config::push_layer(std::string_view name, Ref<Array> overlay, LayerOptions options) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (!overlay)
        return Unexpected(
            make_error(ErrorCode::NotFound, "null overlay for layer '" + Str(name) + "'"));
    if (name == "scratch" || impl_->find_layer(name))
        return Unexpected(refused("there is already a layer named '" + Str(name) + "'"));
    LayerState layer;
    layer.name = Str(name);
    layer.options = options;
    layer.root = std::move(overlay);
    layer.after = impl_->session_below(impl_->layers, impl_->layers.size());
    impl_->layers.push_back(std::move(layer));
    impl_->change(*this, trigger_name("layer"));
    return {};
}

Expected<void> Config::remove_layer(std::string_view name) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (name == "scratch")
        return Unexpected(refused("scratch cannot be removed; clear it instead"));
    const auto at = impl_->find_layer(name);
    if (!at)
        return Unexpected(make_error(ErrorCode::NotFound, "no layer named '" + Str(name) + "'"));
    // The layers above were read with this one's macros; read them again without.
    Vec<LayerState> rest = impl_->layers;
    rest.erase(rest.begin() + static_cast<ptrdiff_t>(*at));
    if (auto r = impl_->reread(rest, *at); !r) return r;
    impl_->layers = std::move(rest);
    impl_->change(*this, trigger_name("layer"));
    return {};
}

Expected<void> Config::pop_layer() {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (impl_->layers.empty())
        return Unexpected(make_error(ErrorCode::NotFound, "no layer to pop"));
    impl_->layers.pop_back();
    impl_->change(*this, trigger_name("layer"));
    return {};
}

List<Symbol> Config::layers() const {
    Vec<Symbol> names;
    for (const auto& l : impl_->layers) names.emplace_back(l.name);
    names.emplace_back("scratch");
    return List<Symbol>(names);
}

Expected<void> Config::set_layer_enabled(std::string_view name, bool enabled) {
    if constexpr (kShipping) return Unexpected(shipping("toggling a layer"));
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    bool* flag = nullptr;
    if (name == "scratch") {
        flag = &impl_->scratch_enabled;
    } else if (auto at = impl_->find_layer(name)) {
        flag = &impl_->layers[*at].options.enabled;
    } else {
        return Unexpected(make_error(ErrorCode::NotFound, "no layer named '" + Str(name) + "'"));
    }
    if (*flag == enabled) return {};
    *flag = enabled;
    impl_->change(*this, trigger_name("layer"));
    return {};
}

Expected<bool> Config::toggle_layer(std::string_view name) {
    const bool next = !layer_enabled(name);
    if (auto r = set_layer_enabled(name, next); !r) return Unexpected(std::move(r).error());
    return next;
}

bool Config::layer_enabled(std::string_view name) const {
    if (name == "scratch") return impl_->scratch_enabled;
    const auto at = impl_->find_layer(name);
    return at && impl_->layers[*at].options.enabled;
}

// --- Reading ---

bool Config::scratch_values(const Path& path, Values& out) const noexcept {
    const ScratchEntry* e = impl_->scratch_entry(path);
    if (!e) return false;
    const Values all = e->values->values();
    if (e->path == path) {
        out = all;
        return true;
    }
    const uint32_t i = path[path.size() - 1].index;
    if (i >= all.size()) return false;
    out = Values(all.nodes().subspan(i, 1), e->values.get());
    return true;
}

Error Config::scratch_type_error(const Path& path, const detail::ConvertInfo& info) {
    return make_error(ErrorCode::Type, quoted(path) + " in scratch is not " + Str(info.name));
}

Expected<Source> Config::source(const Path& path) const {
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    if (path.empty()) return Unexpected(refused("the whole config has no single source"));
    if (impl_->scratch_entry(path)) return Source{Symbol("scratch"), {}, 0};
    std::optional<size_t> at = impl_->supplier(path, impl_->layers.size());
    if (!at) {
        // No layer has values there; name the topmost enabled one that has the path.
        for (size_t i = impl_->layers.size(); i-- > 0 && !at;) {
            detail::Resolved r;
            if (impl_->layers[i].options.enabled &&
                detail::resolve(*impl_->layers[i].root, path, r) == detail::ResolveStatus::Ok)
                at = i;
        }
    }
    if (!at) return Unexpected(detail::get_error(*root_, path, nullptr));
    return impl_->source_in(impl_->layers[*at], path);
}

// --- Reload and telemetry ---

Expected<bool> Config::reload(const ReloadRequest& request) {
    if constexpr (kShipping) return false;
    ReloadRecord rec = impl_->begin(
        request.trigger.empty() ? trigger_name("reload") : request.trigger, request.origin_ns);
    uint64_t t = rec.start_ns;
    if (frozen_ && !impl_->options.live_layers) {
        rec.outcome = ReloadOutcome::Refused;
        impl_->finish(*this, rec, t);
        return false;
    }
    auto& ls = impl_->layers;
    size_t from = ls.size();
    for (size_t i = 0; i < ls.size() && from == ls.size(); ++i)
        if (!ls[i].path.empty() && impl_->changed(ls[i], rec.first_file)) from = i;
    impl_->lap(rec.scan_ns, t);
    if (from == ls.size()) {
        rec.outcome = ReloadOutcome::Unchanged;
        impl_->finish(*this, rec, t);
        return false;
    }
    auto r = impl_->reread(ls, from, &rec.read_ns, &rec.files_reloaded);
    const uint64_t parsed = impl_->now();
    rec.parse_ns = parsed - t - rec.read_ns;
    t = parsed;
    if (!r) {
        rec.outcome = ReloadOutcome::ParseError;
        impl_->finish(*this, rec, t);
        return Unexpected(std::move(r).error());
    }
    impl_->commit(*this, rec, t);
    impl_->finish(*this, rec, t);
    return true;
}

Config::ReloadId Config::on_reload(const Path& group, Callback<void()> rebuild,
                                   Callback<Reload()> migrate) {
    const ReloadId id = impl_->next_id++;
    impl_->subscribers.push_back({id, group, std::move(rebuild), std::move(migrate)});
    return id;
}

Config::ReloadId Config::on_reload_complete(Callback<void(const ReloadRecord&)> callback) {
    const ReloadId id = impl_->next_id++;
    impl_->completions.push_back({id, std::move(callback)});
    return id;
}

void Config::remove_on_reload(ReloadId id) {
    auto& s = impl_->subscribers;
    s.erase(std::remove_if(s.begin(), s.end(), [id](const auto& x) { return x.id == id; }),
            s.end());
    auto& c = impl_->completions;
    c.erase(std::remove_if(c.begin(), c.end(), [id](const auto& x) { return x.id == id; }),
            c.end());
}

void Config::set_time_source(TimeSource fn, void* user) {
    impl_->clock = fn ? fn : steady_now;
    impl_->clock_user = fn ? user : nullptr;
}

void Config::set_reload_history(size_t capacity) {
    impl_->ring = Vec<ReloadRecord>();
    impl_->ring_cap = capacity;
    impl_->ring_next = 0;
    if (capacity) impl_->ring.reserve(capacity);
}

size_t Config::history_size() const noexcept {
    return impl_->ring.size();
}

const ReloadRecord& Config::history_at(size_t i) const noexcept {
    const auto& ring = impl_->ring;
    const size_t oldest = ring.size() < impl_->ring_cap ? 0 : impl_->ring_next;
    return ring[(oldest + i) % ring.size()];
}

std::optional<ReloadRecord> Config::last_reload() const noexcept {
    if (impl_->ring.empty()) return std::nullopt;
    return history_at(impl_->ring.size() - 1);
}

// --- Scratch ---

Expected<void> Config::set(const Path& path, std::span<const Node> values) {
    if constexpr (kShipping) return Unexpected(shipping("set()"));
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    if (path.empty()) return Unexpected(refused("cannot set the whole config"));
    for (size_t i = 0; i + 1 < path.size(); ++i)
        if (path[i].is_index)
            return Unexpected(
                refused("cannot set a value inside an indexed element: " + quoted(path)));
    if (!last_is_index(path)) {
        impl_->put(*this, path, values);
        return {};
    }
    // One element of a value: set the whole value with that element replaced.
    if (values.size() != 1)
        return Unexpected(refused(quoted(path) + " is one element and takes one value"));
    const Path target = parent_of(path);
    Vec<Node> whole;
    if (!impl_->current(*this, target, whole) || path[path.size() - 1].index >= whole.size())
        return Unexpected(detail::get_error(*root_, path, nullptr));
    whole[path[path.size() - 1].index] = values[0];
    impl_->put(*this, target, whole);
    return {};
}

Expected<void> Config::set(std::string_view path, const Node& value) {
    if constexpr (kShipping) return Unexpected(shipping("set()"));
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    // The fast path: a scalar already in scratch changes in place, allocating nothing.
    for (ScratchEntry& e : impl_->scratch) {
        if (e.values->size() == 2 && e.path.matches(path)) {
            e.values->set(1, value);
            scratch_dirty_ = true;
            impl_->retune(e);
            return {};
        }
    }
    return set(Path(path), std::span<const Node>(&value, 1));
}

Expected<void> Config::clear_scratch(std::string_view path) {
    if constexpr (kShipping) return Unexpected(shipping("clear_scratch()"));
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    auto& s = impl_->scratch;
    if (path.empty())
        s.clear();
    else
        s.erase(std::remove_if(s.begin(), s.end(),
                               [&](const ScratchEntry& e) { return e.path.matches(path); }),
                s.end());
    impl_->change(*this, trigger_name("layer"));
    return {};
}

List<Symbol> Config::unsaved() const {
    Vec<Symbol> keys;
    for (const auto& e : impl_->scratch) keys.emplace_back(e.key);
    return List<Symbol>(keys);
}

// --- Write-back ---

Expected<WriteReport> Config::write_back(const Path& path) {
    if constexpr (kShipping) return Unexpected(shipping("write-back"));
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    Vec<Node> values;
    if (!impl_->current(*this, path, values))
        return Unexpected(detail::get_error(*root_, path, nullptr));
    const auto at = impl_->supplier(path, impl_->layers.size());
    if (!at)
        return Unexpected(refused(
            quoted(path) + " is not in any layer's file; use write_back_to to add it to one"));
    return impl_->write_into(*this, *at, path, values, false);
}

Expected<WriteReport> Config::write_back_to(const Path& path, std::string_view layer) {
    if constexpr (kShipping) return Unexpected(shipping("write-back"));
    if (!path.valid()) return Unexpected(detail::get_error(*root_, path, nullptr));
    if (layer == "scratch") return Unexpected(refused("scratch is never written to disk"));
    Vec<Node> values;
    if (!impl_->current(*this, path, values))
        return Unexpected(detail::get_error(*root_, path, nullptr));
    const auto at = impl_->find_layer(layer);
    if (!at)
        return Unexpected(make_error(ErrorCode::NotFound, "no layer named '" + Str(layer) + "'"));
    return impl_->write_into(*this, *at, path, values, true);
}

Expected<WriteReport> Config::promote(std::string_view from, std::string_view to) {
    if constexpr (kShipping) return Unexpected(shipping("promote()"));
    if (auto ok = impl_->may_change(*this); !ok) return Unexpected(std::move(ok).error());
    if (to == "scratch") return Unexpected(refused("scratch is never written to disk"));
    const auto ti = impl_->find_layer(to);
    const auto fi = impl_->find_layer(from);
    if (!ti || (!fi && from != "scratch"))
        return Unexpected(
            make_error(ErrorCode::NotFound, "no layer named '" + Str(ti ? from : to) + "'"));
    if (fi == ti) return Unexpected(refused("cannot promote a layer into itself"));
    const Ref<Array> tree = fi ? impl_->layers[*fi].root : impl_->scratch_root();
    const Vec<Leaf> leaves = tree ? leaves_of(*tree) : Vec<Leaf>();

    // Plan every entry against the target as each earlier one leaves it, in memory.
    HashMap<Str, Str> virt;
    const LoadSession::Reader disk = impl_->base.reader();
    const LoadSession::Reader vread = [&](std::string_view p, TextSink out) {
        if (auto it = virt.find(Str(p)); it != virt.end()) {
            out(it->second);
            return true;
        }
        return disk(p, out);
    };
    LayerState target = impl_->layers[*ti];
    Vec<Error> refusals;
    for (const Leaf& leaf : leaves) {
        auto p = impl_->plan(target, Path(leaf.path), leaf.values, true, vread);
        if (!p) {
            refusals.push_back(std::move(p).error());
            continue;
        }
        if (!p->changed) continue;
        virt[Str(p->file.str())] = std::move(p->text);
        LoadSession before = *impl_->session_below(impl_->layers, *ti);
        before.set_reader(vread);
        if (auto r = impl_->read(target, before); !r) refusals.push_back(std::move(r).error());
    }
    WriteReport report;
    if (!refusals.empty()) {  // nothing written
        report.refused = List<Error>(refusals);
        return report;
    }
    for (const auto& [file, text] : virt)
        if (!impl_->writer(file, text))
            return Unexpected(
                make_error(ErrorCode::Io, "cannot write '" + file + "'", Symbol(file)));
    ReloadRecord rec = impl_->begin(trigger_name("write_back"));
    uint64_t t = rec.start_ns;
    if (!virt.empty()) {
        const uint64_t t0 = impl_->now();
        auto r = impl_->reread(impl_->layers, *ti, &rec.read_ns, &rec.files_reloaded);
        t = impl_->now();
        rec.parse_ns = t - t0 - rec.read_ns;
        if (!r) {
            rec.outcome = ReloadOutcome::ParseError;
            impl_->finish(*this, rec, t);
            return Unexpected(std::move(r).error());
        }
    }
    impl_->commit(*this, rec, t);
    impl_->finish(*this, rec, t);
    Vec<WrittenLine> written;
    if (!virt.empty())
        for (const Leaf& leaf : leaves) {
            const Source s = impl_->source_in(impl_->layers[*ti], Path(leaf.path));
            written.push_back({s.file, s.line});
        }
    report.written = List<WrittenLine>(written);
    return report;
}

// --- Layer tooling ---

Expected<List<LayerEffectEntry>> Config::layer_effect(std::string_view layer) const {
    const bool is_scratch = layer == "scratch";
    const auto li = impl_->find_layer(layer);
    if (!li && !is_scratch)
        return Unexpected(make_error(ErrorCode::NotFound, "no layer named '" + Str(layer) + "'"));
    const Ref<Array> tree = li ? impl_->layers[*li].root : impl_->scratch_root();
    Vec<LayerEffectEntry> out;
    if (!tree) return List<LayerEffectEntry>();
    // The stack without this layer.
    Ref<Array> without = Array::make(0);
    for (size_t i = 0; i < impl_->layers.size(); ++i)
        if (impl_->layers[i].options.enabled && (!li || i != *li))
            without = moira::merge(*without, *impl_->layers[i].root);
    const Ref<Array> scratch = impl_->scratch_enabled ? impl_->scratch_root() : nullptr;
    if (!is_scratch && scratch) without = moira::merge(*without, *scratch);

    for (const Leaf& leaf : leaves_of(*tree)) {
        LayerEffectEntry e{};
        const Path path(leaf.path);
        e.path = Symbol(leaf.path);
        e.value = pack(leaf.values);
        e.source = is_scratch ? Source{Symbol("scratch"), {}, 0}
                              : Source{Symbol(layer), leaf.holder->file(), leaf.holder->line()};
        Vec<Node> below;
        if (value_nodes(*without, path, below)) e.below = pack(below);
        const bool scratch_has = !is_scratch && impl_->scratch_entry(path);
        if (scratch_has) {
            e.below_source = Source{Symbol("scratch"), {}, 0};
        } else if (auto s = impl_->supplier(path, impl_->layers.size(), li ? *li : SIZE_MAX)) {
            e.below_source = impl_->source_in(impl_->layers[*s], path);
        }
        e.shadowed = scratch_has;
        if (li)
            for (size_t i = *li + 1; i < impl_->layers.size() && !e.shadowed; ++i)
                e.shadowed = impl_->supplier(path, i + 1, SIZE_MAX) == i;
        out.push_back(std::move(e));
    }
    return List<LayerEffectEntry>(out);
}

Expected<List<DiffEntry>> Config::diff(std::string_view a, std::string_view b) const {
    auto tree_of = [&](std::string_view name) -> Expected<Ref<Array>> {
        if (name == "scratch") {
            Ref<Array> s = impl_->scratch_root();
            return s ? s : Array::make(0);
        }
        if (auto i = impl_->find_layer(name)) return impl_->layers[*i].root;
        return Unexpected(make_error(ErrorCode::NotFound, "no layer named '" + Str(name) + "'"));
    };
    auto ta = tree_of(a), tb = tree_of(b);
    if (!ta) return Unexpected(std::move(ta).error());
    if (!tb) return Unexpected(std::move(tb).error());
    const Vec<Leaf> la = leaves_of(**ta), lb = leaves_of(**tb);
    auto source = [](std::string_view layer, const Leaf& l) {
        return layer == "scratch" ? Source{Symbol("scratch"), {}, 0}
                                  : Source{Symbol(layer), l.holder->file(), l.holder->line()};
    };
    Vec<DiffEntry> out;
    for (const Leaf& x : la) {
        const auto y =
            std::find_if(lb.begin(), lb.end(), [&](const Leaf& l) { return l.path == x.path; });
        if (y != lb.end() && same_nodes(x.values, y->values)) continue;
        DiffEntry d{Symbol(x.path), pack(x.values), std::nullopt, source(a, x), std::nullopt};
        if (y != lb.end()) {
            d.b = pack(y->values);
            d.source_b = source(b, *y);
        }
        out.push_back(std::move(d));
    }
    for (const Leaf& y : lb)
        if (std::none_of(la.begin(), la.end(), [&](const Leaf& l) { return l.path == y.path; }))
            out.push_back(
                {Symbol(y.path), std::nullopt, pack(y.values), std::nullopt, source(b, y)});
    return List<DiffEntry>(out);
}

// --- Snapshots ---

ConfigSnapshot Config::snapshot() const {
    auto* state = detail::create<ConfigSnapshot::State>(impl_->alloc, impl_->alloc);
    state->layers = impl_->layers;
    state->scratch = copy_scratch(impl_->scratch);
    state->scratch_enabled = impl_->scratch_enabled;
    ConfigSnapshot s;
    s.state_ = state;
    return s;
}

Expected<void> Config::restore(const ConfigSnapshot& snapshot) {
    if (auto ok = impl_->may_change(*this); !ok) return ok;
    if (snapshot.empty()) return Unexpected(make_error(ErrorCode::NotFound, "empty snapshot"));
    impl_->layers = snapshot.state_->layers;
    impl_->scratch = copy_scratch(snapshot.state_->scratch);
    impl_->scratch_enabled = snapshot.state_->scratch_enabled;
    impl_->change(*this, trigger_name("restore"));
    return {};
}

}  // namespace moira
