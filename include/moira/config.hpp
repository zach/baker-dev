#pragma once

// Config: layered, hot-reloadable configuration with provenance and write-back.
//
//   moira::Config cfg;
//   cfg.push_layer("default", "default.dta");
//   cfg.push_layer("platform", "ps5.dta");
//   cfg.push_layer("local", "local/" + user + ".dta", {.optional = true});   // per user
//   cfg.push_layer("patch-2026-10-11", overlay);       // a live-ops overlay array
//   cfg.remove_layer("patch-2026-10-11");              // rollback: leaves no trace
//
//   float c = cfg.get<float>("renderer.clear_color.0");
//   moira::Source s = cfg.source("renderer.clear_color");   // layer, file, line
//
// Layers merge bottom to top. For each tagged array of a layer, the array with the same
// tag below is merged into recursively; the layer's values (everything that is not a tagged
// array) replace the ones below, so (width 3) over (width 2 (range 0 10)) gives
// (width 3 (range 0 10)). Each layer is read in its own pass, but macros and defines carry
// upward from the layers below, as from earlier files in one load. load() and merge() are
// shorthands that push a layer named after the file. A layer pushed with
// {.optional = true} whose file is missing is an empty layer, and reload() picks the file
// up once it exists. A disabled layer keeps its place and its trees but is skipped by
// merging, provenance, and write-back; toggling it is how variants are compared A/B.
//
// Every change -- pushing, popping, removing, enabling, or disabling a layer, reload(),
// restore() -- re-merges, rebinds the tweaks of the attached TweakRegistry, fires the
// reload callbacks, and appends a ReloadRecord to the history. Old trees stay alive while
// anyone holds a Ref to them, so a reload never dangles a pointer someone is reading
// through.
//
// Hot reload. reload() re-reads every file each file layer was built from (includes too),
// and re-parses the lowest layer whose files changed and the layers above it. A reload
// that fails to parse changes nothing. Callbacks are registered per group, the subtree that
// code owns: on_reload("renderer", rebuild) fires only when that subtree changed. Punting
// -- tearing the subsystem down and rebuilding it from the new values -- is the default
// and needs no code beyond `rebuild`; a `migrate` callback can move runtime state over
// instead and return Reload::Migrated, or decline with Reload::Punt. By convention a
// subsystem reads only its own group: that is what makes a group the unit of refresh.
//
// Telemetry. Each change is timed in six stages (scan, read, parse, merge, bind, notify)
// with the time source the host gives set_time_source (default: steady_clock), and kept in
// a fixed ring (set_reload_history, default 256). Under MOIRA_SHIPPING the timing compiles
// out: last_reload() is empty and each_reload() visits nothing.
//
// Scratch and write-back. set() puts a value tuned in the running program into the
// reserved in-memory layer "scratch", always topmost, so source() reports it as unsaved.
// Setting a path already in scratch is a fast path: no re-merge and no allocation; only
// the tweaks bound to that path are updated. Lookups through Config (get, source) consult
// scratch first; root() includes scratch as of the last full merge. write_back() writes
// the current value into the file of the highest enabled layer below scratch that supplies
// it, replacing only the tokens that produced the old value, so comments, notes, and
// formatting are kept and the diff is one line. write_back_to() writes into a chosen
// layer's file, adding the entry as the last child of the deepest ancestor written there
// (creating an optional layer's missing file); promote() writes a whole layer's effect into
// another layer, all or nothing. Write-back refuses, with a located error, a value from a
// macro, an overlay, an ancestor that came from #include or #merge, a missing indexed
// element, or a file that changed on disk since it was read; it never guesses. It needs
// edit mode (ConfigOptions::edit_mode), on by default except under MOIRA_SHIPPING.
//
// snapshot() captures the layer stack, each layer's trees (shared, not copied), and
// scratch; restore() returns to it exactly without reading any file.
//
// freeze() ends the initial load: afterwards reload, layer changes, and live edits are
// refused, unless ConfigOptions::live_layers explicitly allows them (the path a shipped
// game uses for live-ops overlays). Under MOIRA_SHIPPING, reload(), toggles, set(),
// write-back, and promote() are always inert.
//
// Not thread-safe: change a Config from one thread. Readers on other threads should take
// root() (a Ref) and read that.

#include <moira/array.hpp>
#include <moira/clock.hpp>
#include <moira/parse.hpp>
#include <moira/path.hpp>
#include <moira/source_map.hpp>
#include <moira/tweak.hpp>

#include <moira/callback.hpp>
#include <moira/list.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace moira {

enum class MergePolicy : uint8_t {
    Overwrite,    // Config layers: the incoming values replace the existing ones
    FillMissing,  // #merge: existing values stay; only missing keys are added (D1)
};

// Merges `over` into `base`: tagged arrays recurse by tag, anything else is a value.
// Returns `base` itself if nothing changed; never modifies either.
[[nodiscard]] Ref<Array> merge(const Array& base, const Array& over,
                               MergePolicy policy = MergePolicy::Overwrite);

// Where a value came from.
// Every member has its own initializer, so that {layer, file, line} leaves the rest empty
// without -Wmissing-field-initializers.
struct Source {
    Symbol layer{};  // "scratch" for an unsaved live edit
    Symbol file{};   // empty for a layer with no file (an overlay, scratch)
    int32_t line = 0;
    // When the file declares (origin "economy.xlsx" (tool sheets-export)): what it was
    // generated from, and by what (parse.hpp). Empty otherwise.
    Symbol origin{};
    Symbol tool{};
};

enum class Reload : uint8_t { Migrated, Punt };

struct ConfigOptions {
    bool edit_mode = !kShipping;  // keep source maps, for write-back
    bool live_layers = false;     // allow layer changes and live edits after freeze()
    // The registry whose tweaks this config binds; null for none.
    TweakRegistry* tweaks = &TweakRegistry::global();
    // Outside edit mode, file layers load through binary caches kept here (cache.hpp,
    // D70): a fresh cache instead of the text, else the text and a new cache, written
    // through the Config's writer. Copied by the Config; empty: no caches. The directory
    // must exist. Shipping builds write none: run the same layers through a development
    // build's Config (a build step), and ship the directory.
    std::string_view cache_dir = {};
};

struct LayerOptions {
    bool optional = false;  // a missing file is not an error; the layer exists, empty
    bool enabled = true;    // disabled layers keep their position but are skipped by merge
};

// --- Telemetry ---

enum class ReloadOutcome : uint8_t { Ok, Unchanged, ParseError, Refused };

struct ReloadRequest {
    Symbol trigger{};        // host tag: "watcher", "console", "livelink", ...
    uint64_t origin_ns = 0;  // when the edit was saved, in the TimeSource domain; 0: unknown
};

struct ReloadRecord {
    uint64_t epoch = 0;  // epoch after this change (unchanged if nothing changed)
    Symbol trigger{};    // the request's, or "reload", "layer", "restore", "write_back"
    uint64_t origin_ns = 0, start_ns = 0, end_ns = 0;
    uint64_t scan_ns = 0, read_ns = 0, parse_ns = 0, merge_ns = 0, bind_ns = 0, notify_ns = 0;
    uint32_t files_reloaded = 0, tweaks_changed = 0;
    uint32_t migrated = 0, punted = 0;  // reload-callback outcomes
    Symbol first_file{};                // the first changed file, for display
    Symbol layer{};                     // the layer of first_file; empty if none changed
    uint64_t visible_ns = 0;            // when the host next presented (mark_visible); 0: not yet
    ReloadOutcome outcome = ReloadOutcome::Ok;
};

// Loop statistics (spec §4, D90 Q1, D92): how long edits take to be seen, per file and per
// layer. Only reloads that changed a file feed them, so a host that polls for changes does
// not push edits out. Times are kept to the microsecond.
struct LoopRow {
    Symbol name;            // a file, or a layer
    uint32_t reloads = 0;   // reloads that changed it, failures included, since it got its row
    uint32_t failures = 0;  // of those, the ones that failed to parse
    // Over its last 32 edits that reloaded: edit-to-see, from when the edit was saved
    // (ReloadRequest::origin_ns) to when the host presented it (mark_visible), or to the end
    // of the reload if the host has not; and the reload's own time.
    uint32_t edits = 0;  // edit-to-see samples: only reloads that gave an origin have one
    uint64_t edit_median_ns = 0, edit_worst_ns = 0;
    uint32_t times = 0;  // reload-time samples
    uint64_t reload_median_ns = 0, reload_worst_ns = 0;
};

struct LoopStats {
    uint32_t reloads = 0;    // calls to reload()
    uint32_t unchanged = 0;  // of those, the ones that found nothing changed
    uint32_t failures = 0;   // the ones that failed to parse, or were refused
    // The 32 files and 16 layers changed most recently, most recent first.
    List<LoopRow> files, layers;
};

// --- Layer tooling ---

// One value a layer sets: a path to a tagged array with values. A value of several nodes,
// like (screen_size 1920 1080), is an untagged array of them: (1920 1080).
struct LayerEffectEntry {
    Symbol path;                // dotted, as get<T>() takes it
    Node value;                 // this layer's value
    std::optional<Node> below;  // the stack's value without this layer, if any
    Source source;              // provenance, as source() returns it
    std::optional<Source> below_source;
    bool shadowed = false;  // a higher enabled layer (or scratch) overrides this path
};

struct DiffEntry {
    Symbol path;
    std::optional<Node> a, b;  // empty: that layer does not set this path
    std::optional<Source> source_a, source_b;
};

struct WrittenLine {
    Symbol file;
    int32_t line = 0;
};

// The edit journal (spec §4, D90 Q2, D93): every live change made through the Config, and
// what became of it. A value of more than four nodes keeps its first four, and its count.
enum class EditFate : uint8_t {
    Unsaved,     // still only in scratch
    Written,     // written to a file by write_back, write_back_to, or promote from scratch
    Reverted,    // dropped from scratch by clear_scratch or restore
    Superseded,  // edited again before it was saved: the later entry has the fate
};

struct JournalEntry {
    Symbol path;           // as unsaved() names it: box/width
    uint64_t time_ns = 0;  // from the Config's time source
    Node before[4], after[4];
    uint32_t before_count = 0;  // 0: the path had no value
    uint32_t after_count = 0;
    Source source;  // where the old value came from
    EditFate fate = EditFate::Unsaved;
    WrittenLine written;  // for Written: the file and line
};

struct WriteReport {
    List<WrittenLine> written;  // every file and line written
    List<Error> refused;        // promote(): every refusal; then nothing was written
    [[nodiscard]] bool ok() const noexcept { return refused.empty(); }
};

// A configuration state to return to: the layer stack, each layer's trees (shared by
// reference, not copied), the files' load-time content, and a copy of scratch. Copyable.
class ConfigSnapshot {
public:
    ConfigSnapshot() noexcept = default;
    ConfigSnapshot(const ConfigSnapshot& other) noexcept;
    ConfigSnapshot& operator=(const ConfigSnapshot& other) noexcept;
    ~ConfigSnapshot();
    [[nodiscard]] bool empty() const noexcept { return !state_; }

private:
    friend class Config;
    struct State;  // refcounted; defined in config.cpp
    State* state_ = nullptr;
};

class Config {
public:
    using Writer = Callback<bool(std::string_view path, std::string_view text)>;
    using ReloadId = uint32_t;

    // Everything the Config keeps comes from `allocator`, which must outlive it and its
    // snapshots (D61).
    explicit Config(ConfigOptions options = {}, const Allocator& allocator = default_allocator());
    ~Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // --- Layers. ---
    [[nodiscard]] Expected<void> push_layer(std::string_view name, std::string_view path,
                                            LayerOptions options = {});
    [[nodiscard]] Expected<void> push_layer(std::string_view name, Ref<Array> overlay,
                                            LayerOptions options = {});
    [[nodiscard]] Expected<void> remove_layer(std::string_view name);
    // Removes the topmost layer below scratch.
    [[nodiscard]] Expected<void> pop_layer();
    [[nodiscard]] Expected<void> load(std::string_view path) { return push_layer(path, path); }
    [[nodiscard]] Expected<void> merge(std::string_view path) { return push_layer(path, path); }
    // Layer names bottom to top, ending with "scratch".
    [[nodiscard]] List<Symbol> layers() const;
    [[nodiscard]] Expected<void> set_layer_enabled(std::string_view name, bool enabled);
    // Returns the new state.
    [[nodiscard]] Expected<bool> toggle_layer(std::string_view name);
    // False for an unknown layer.
    [[nodiscard]] bool layer_enabled(std::string_view name) const;

    // --- Reading. ---
    // The merged tree, including scratch as of the last full merge. Holding the Ref keeps
    // this version alive across reloads.
    [[nodiscard]] Ref<Array> root() const noexcept { return root_; }
    template <class T>
    [[nodiscard]] Expected<T> get(const Path& path) const {
        Values v;
        if (scratch_dirty_ && scratch_values(path, v)) return from_scratch<T>(path, v);
        return root_->get<T>(path);
    }
    template <class T>
    [[nodiscard]] Expected<T> get(const Path& path, T fallback) const {
        Values v;
        if (scratch_dirty_ && scratch_values(path, v)) return from_scratch<T>(path, v);
        return root_->get<T>(path, std::move(fallback));
    }
    [[nodiscard]] Expected<Source> source(const Path& path) const;
    // Increments on every change but a fast-path set().
    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }

    // --- Hot reload and telemetry. ---
    // Re-reads changed files; true if anything changed.
    [[nodiscard]] Expected<bool> reload(const ReloadRequest& request = {});
    ReloadId on_reload(const Path& group, Callback<void()> rebuild,
                       Callback<Reload()> migrate = {});
    // Called after every change with its finished record (not under MOIRA_SHIPPING).
    ReloadId on_reload_complete(Callback<void(const ReloadRecord&)> callback);
    void remove_on_reload(ReloadId id);
    void set_time_source(TimeSource fn, void* user = nullptr);
    // Allocates the history ring once; 0 turns history off. Default 256.
    void set_reload_history(size_t capacity);
    [[nodiscard]] std::optional<ReloadRecord> last_reload() const noexcept;
    // Called by the host when a frame presents: stamps visible_ns on the reloads completed
    // since the last call, and counts their edit-to-see to now. With nothing to stamp, one
    // branch. Does nothing under MOIRA_SHIPPING.
    void mark_visible() noexcept {
        if (visible_pending_) stamp_visible();
    }
    // Per file and per layer (above); empty under MOIRA_SHIPPING. Allocates its result.
    [[nodiscard]] LoopStats loop_stats() const;
    // Visits the history, oldest to newest.
    template <class F>
    void each_reload(F&& fn) const {
        for (size_t i = 0, n = history_size(); i < n; ++i) fn(history_at(i));
    }
    // Called with a located error for each tweak whose value does not convert (the tweak
    // keeps its default) and each bad tweak path. Default: ignored.
    void set_warning_handler(Callback<void(const Error&)> handler);
    // Called after every merge (a layer pushed, popped, removed, or toggled; a reload; a
    // restore) with the merged root and the warning handler, to report on the data:
    // moira::shape::validator() checks shapes this way (D84). Null: none.
    using Validator = Callback<void(const Array& root, const Callback<void(const Error&)>& warn)>;
    void set_validator(Validator validator);
    void freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }

    // The session each layer is read with: include_dirs, defines, limits, reader.
    [[nodiscard]] LoadSession& session() noexcept;
    // Replaces file writing for write-back (default: the local filesystem).
    void set_writer(Writer writer);

    // --- Scratch, write-back, and layer tooling. ---
    [[nodiscard]] Expected<void> set(const Path& path, std::span<const Node> values);
    [[nodiscard]] Expected<void> set(std::string_view path, const Node& value);
    template <class T>
    [[nodiscard]] Expected<void> set(const Path& path, const T& value) {
        ValueList list;
        Convert<T>::to(value, list);
        return set(path, std::span<const Node>(list.nodes()));
    }
    // One path, or all of scratch.
    [[nodiscard]] Expected<void> clear_scratch(std::string_view path = {});
    // Paths in scratch: live edits not yet written back.
    [[nodiscard]] List<Symbol> unsaved() const;
    // Visits the edit journal, oldest first: up to the last 256 live changes (D93); empty
    // under MOIRA_SHIPPING. set_journal_history allocates the ring once; 0 turns it off.
    template <class F>
    void journal(F&& fn) const {
        for (size_t i = 0, n = journal_size(); i < n; ++i) fn(journal_at(i));
    }
    void set_journal_history(size_t capacity);

    [[nodiscard]] Expected<WriteReport> write_back(const Path& path);
    [[nodiscard]] Expected<WriteReport> write_back_to(const Path& path, std::string_view layer);
    // Writes every entry of layer_effect(from) into `to`'s file, or nothing: on any refusal
    // the report lists them all and no file changes. `from` stays in the stack.
    [[nodiscard]] Expected<WriteReport> promote(std::string_view from, std::string_view to);
    [[nodiscard]] Expected<List<LayerEffectEntry>> layer_effect(std::string_view layer) const;
    [[nodiscard]] Expected<List<DiffEntry>> diff(std::string_view a, std::string_view b) const;

    [[nodiscard]] ConfigSnapshot snapshot() const;
    [[nodiscard]] Expected<void> restore(const ConfigSnapshot& snapshot);

private:
    struct Impl;
    friend class TweakBase;

    void bind(TweakBase& t);
    // The scratch values `path` names (exactly, or one element of them), if any.
    [[nodiscard]] bool scratch_values(const Path& path, Values& out) const noexcept;
    [[nodiscard]] static Error scratch_type_error(const Path& path,
                                                  const detail::ConvertInfo& info);
    template <class T>
    [[nodiscard]] static Expected<T> from_scratch(const Path& path, const Values& v) {
        T out{};
        if (Convert<T>::from(v, out)) return out;
        return Unexpected(scratch_type_error(path, detail::convert_info<T>()));
    }
    void stamp_visible() noexcept;
    [[nodiscard]] size_t history_size() const noexcept;
    [[nodiscard]] const ReloadRecord& history_at(size_t i) const noexcept;
    [[nodiscard]] size_t journal_size() const noexcept;
    [[nodiscard]] const JournalEntry& journal_at(size_t i) const noexcept;

    Ref<Array> root_;
    uint64_t epoch_ = 0;
    bool frozen_ = false;
    bool scratch_dirty_ = false;    // scratch changed since the last full merge
    bool visible_pending_ = false;  // a reload waits for mark_visible
    Impl* impl_;
};

}  // namespace moira
