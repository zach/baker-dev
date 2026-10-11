#pragma once

// Config: layered, hot-reloadable configuration with provenance and write-back.
//
//   moira::Config cfg;
//   cfg.push_layer("default", "default.dta");
//   cfg.push_layer("platform", "ps5.dta");
//   cfg.push_layer("local", "local.dta");             // per-machine, never checked in
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
// shorthands that push a layer named after the file.
//
// Every change -- pushing, popping or removing a layer, reload(), a live edit -- re-merges,
// rebinds the tweaks of the attached TweakRegistry, and fires the reload callbacks. Old
// trees stay alive while anyone holds a Ref to them, so a reload never dangles a pointer
// someone is reading through.
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
// Live edits and write-back. set() changes a value in the running program: it goes into a
// "live" layer above all others, so source() reports it as unsaved. write_back() writes the
// current value into the file of the layer that would otherwise supply it, replacing only
// the tokens that produced the old value, so comments, notes, and formatting are kept and
// the diff is one line. It refuses, with a located error, a value that came from a macro,
// an overlay layer, an array assembled by #merge, or a file that changed since it was read;
// it never guesses. write_back_to() writes into a different layer's file, adding the key
// if it is missing there. Write-back needs edit mode (ConfigOptions::edit_mode), on by
// default except under MOIRA_SHIPPING.
//
// freeze() ends the initial load: afterwards reload, layer changes, and live edits are
// refused, unless ConfigOptions::live_layers explicitly allows them (the path a shipped
// game uses for live-ops overlays). Under MOIRA_SHIPPING, reload() and live edits are
// always inert.
//
// Not thread-safe: change a Config from one thread. Readers on other threads should take
// root() (a Ref) and read that.

#include <moira/array.hpp>
#include <moira/parse.hpp>
#include <moira/path.hpp>
#include <moira/source_map.hpp>
#include <moira/tweak.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
struct Source {
    std::string layer;  // "live" for an unsaved live edit
    Symbol file;        // empty for a layer with no file (an overlay, a live edit)
    int32_t line = 0;
};

enum class Reload : uint8_t { Migrated, Punt };

struct ConfigOptions {
    bool edit_mode = !kShipping;  // keep source maps, for write-back
    bool live_layers = false;     // allow layer changes and live edits after freeze()
    // The registry whose tweaks this config binds; null for none.
    TweakRegistry* tweaks = &TweakRegistry::global();
};

class Config {
public:
    using Writer = std::function<bool(const std::string& path, std::string_view text)>;
    using ReloadId = uint32_t;

    explicit Config(ConfigOptions options = {});
    ~Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    // --- Layers. ---
    [[nodiscard]] Expected<void> try_push_layer(std::string_view name, std::string_view path);
    [[nodiscard]] Expected<void> try_push_layer(std::string_view name, Ref<Array> overlay);
    [[nodiscard]] Expected<void> try_remove_layer(std::string_view name);
    [[nodiscard]] Expected<void> try_pop_layer();
    [[nodiscard]] Expected<void> try_load(std::string_view path) {
        return try_push_layer(path, path);
    }
    [[nodiscard]] Expected<void> try_merge(std::string_view path) {
        return try_push_layer(path, path);
    }
    [[nodiscard]] std::vector<std::string> layers() const;

    // --- Reading. ---
    // The merged tree. Holding the Ref keeps this version alive across reloads.
    [[nodiscard]] Ref<Array> root() const noexcept { return root_; }
    template <class T>
    [[nodiscard]] Expected<T> try_get(const Path& path) const {
        return root_->try_get<T>(path);
    }
    template <class T>
    [[nodiscard]] Expected<T> try_get(const Path& path, T fallback) const {
        return root_->try_get<T>(path, std::move(fallback));
    }
    [[nodiscard]] Expected<Source> try_source(const Path& path) const;
    // Increments on every change.
    [[nodiscard]] uint64_t epoch() const noexcept { return epoch_; }

    // --- Hot reload. ---
    // Re-reads changed files; true if anything changed.
    [[nodiscard]] Expected<bool> try_reload();
    ReloadId on_reload(const Path& group, std::function<void()> rebuild,
                       std::function<Reload()> migrate = {});
    void remove_on_reload(ReloadId id);
    // Called with a located error for each tweak whose value does not convert (the tweak
    // keeps its default) and each bad tweak path. Default: ignored.
    void set_warning_handler(std::function<void(const Error&)> handler);
    void freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }

    // The session each layer is read with: include_dirs, defines, limits, reader.
    [[nodiscard]] LoadSession& session() noexcept;
    // Replaces file writing for write-back (default: the local filesystem).
    void set_writer(Writer writer);

    // --- Live edits and write-back. ---
    [[nodiscard]] Expected<void> try_set(const Path& path, std::span<const Node> values);
    template <class T>
    [[nodiscard]] Expected<void> try_set(const Path& path, const T& value) {
        ValueList list;
        Convert<T>::to(value, list);
        return try_set(path, std::span<const Node>(list.nodes()));
    }
    [[nodiscard]] Expected<void> try_write_back(const Path& path);
    [[nodiscard]] Expected<void> try_write_back_to(const Path& path, std::string_view layer);
    // Paths with live edits not yet written back.
    [[nodiscard]] std::vector<std::string> unsaved() const;

#if MOIRA_EXCEPTIONS
    void push_layer(std::string_view name, std::string_view path) {
        check(try_push_layer(name, path));
    }
    void push_layer(std::string_view name, Ref<Array> overlay) {
        check(try_push_layer(name, std::move(overlay)));
    }
    void remove_layer(std::string_view name) { check(try_remove_layer(name)); }
    void pop_layer() { check(try_pop_layer()); }
    void load(std::string_view path) { check(try_load(path)); }
    void merge(std::string_view path) { check(try_merge(path)); }
    template <class T>
    [[nodiscard]] T get(const Path& path) const {
        return root_->get<T>(path);
    }
    template <class T>
    [[nodiscard]] T get(const Path& path, T fallback) const {
        return root_->get<T>(path, std::move(fallback));
    }
    [[nodiscard]] Source source(const Path& path) const;
    bool reload();
    template <class T>
    void set(const Path& path, const T& value) {
        check(try_set(path, value));
    }
    void write_back(const Path& path) { check(try_write_back(path)); }
    void write_back_to(const Path& path, std::string_view layer) {
        check(try_write_back_to(path, layer));
    }
#endif

private:
    struct Impl;
    friend class TweakBase;

#if MOIRA_EXCEPTIONS
    static void check(Expected<void> r) {
        if (!r) detail::raise(std::move(r).error());
    }
#endif
    void bind(TweakBase& t);

    Ref<Array> root_;
    uint64_t epoch_ = 0;
    bool frozen_ = false;
    std::unique_ptr<Impl> impl_;
};

}  // namespace moira
