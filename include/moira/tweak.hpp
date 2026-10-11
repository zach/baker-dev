#pragma once

// Tweak<T>: a constant that is configured, hot-reloadable, and editable, in one line.
//
//   // Before:
//   const float kBoxWidth = 2.0f;
//   // After:
//   static moira::Tweak<float> kBoxWidth{"box.width", 2.0f,
//                                        {.range = {0, 10}, .step = 1, .desc = "Box width"}};
//
//   float w = kBoxWidth;   // one relaxed atomic load
//
// The constructor registers the tweak with a TweakRegistry (the global one unless another
// is passed). Registration allocates nothing and does not depend on static-initialization
// order. The Config attached to the registry binds the tweak when it loads, and rebinds it
// on every reload or layer change; a tweak registered after loading binds at once. If the
// path is absent the default stands and bound() is false. That is never an error.
//
// Declare tweaks at namespace scope: a function-local static adds a guard check to every
// read. A T whose std::atomic is lock-free is read with one relaxed load; any other T is
// read through get(), whose reference stays valid until the next reload. Values change only
// when the Config does, on the thread that changes it (single-threaded mutation).
//
// Metadata may be given in code (TweakMeta) or in the data file, and the data file wins:
//
//   (box (width 2 (range 0 10) (step 1) (desc "Box width") (note "try 2.5 on handheld")))
//
// `note` is data-only: a remark from whoever tuned the value for whoever tunes it next.
// `kind` is a presentation hint for tools (color, angle, percent, path, ...); when absent,
// a 3- or 4-element value ranged 0..1 is reported as "color".
//
// Under MOIRA_SHIPPING a tweak marked dev_only ignores the data and keeps its default, and
// set() is refused: values are frozen at what was loaded.

#include <moira/array.hpp>
#include <moira/path.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace moira {

class Config;
class TweakRegistry;
struct Source;

struct TweakRange {
    double min = 0;
    double max = 0;
    [[nodiscard]] bool empty() const noexcept { return !(min < max); }
};

// Every member has its own initializer, so that clang's -Wmissing-field-initializers
// accepts a designated initializer naming only some: {.range = {0, 10}, .step = 1}.
struct TweakMeta {
    TweakRange range{};
    double step = 0;  // 0: none
    std::string_view desc{};
    std::string_view kind{};
    std::string_view group{};  // default: the path without its last segment
    bool dev_only = false;     // under MOIRA_SHIPPING, ignore the data and keep the default
};

class TweakBase {
public:
    TweakBase(const TweakBase&) = delete;
    TweakBase& operator=(const TweakBase&) = delete;

    [[nodiscard]] std::string_view path() const noexcept { return path_; }
    [[nodiscard]] const TweakMeta& code_meta() const noexcept { return meta_; }
    // True while the config supplies the value; false while the default stands.
    [[nodiscard]] bool bound() const noexcept { return bound_ != nullptr; }
    [[nodiscard]] TweakRegistry& registry() const noexcept { return *registry_; }

    // Metadata, the data file's winning over the code's.
    [[nodiscard]] TweakRange range() const;
    [[nodiscard]] double step() const;
    [[nodiscard]] std::string_view desc() const;
    [[nodiscard]] std::string_view note() const;
    [[nodiscard]] std::string_view kind() const;
    [[nodiscard]] std::string_view group() const;

    // The current value as data, for a generic tuning panel (empty if T cannot be written).
    [[nodiscard]] virtual std::vector<Node> values() const = 0;
    // Sets the value from data, as a panel would; see Tweak<T>::set.
    [[nodiscard]] Expected<void> set_values(std::span<const Node> values);

    // Where the current value came from: layer, file, and line (Config::source).
    [[nodiscard]] Expected<Source> source() const;
    // Writes the current value back to the file it came from (Config::write_back).
    [[nodiscard]] Expected<void> write_back();

    // Called after the value changes, on the thread that changed it.
    void on_change(std::function<void()> callback);

protected:
    TweakBase(const char* path, const TweakMeta& meta, TweakRegistry& registry) noexcept
        : path_(path), meta_(meta), registry_(&registry) {}
    ~TweakBase() = default;

    // Registers with the registry, binding at once if its config is loaded. Called by
    // Tweak<T> once its value exists.
    void attach();
    void detach() noexcept;

private:
    friend class TweakRegistry;
    friend class Config;

    // Reads the values as T and, if `apply`, stores them; false if they do not convert.
    virtual bool assign(const Values& values, bool apply) = 0;
    virtual void reset() = 0;  // back to the default
    [[nodiscard]] virtual detail::ConvertInfo info() const noexcept = 0;

    // Rebinds against `root` (a Config's merged tree); true if the value changed. Reports
    // a value that does not convert through `warn`.
    bool bind(const Array& root, const std::function<void(const Error&)>& warn);
    bool unbind();
    void notify();
    const Path& parsed();
    [[nodiscard]] const Array* meta_array(Symbol key) const;

    const char* path_;
    TweakMeta meta_;
    TweakRegistry* registry_;
    TweakBase* prev_ = nullptr;
    TweakBase* next_ = nullptr;
    // Created on first use, not at registration: even an empty std::vector or
    // std::string allocates in MSVC's debug library, and registration allocates nothing.
    std::unique_ptr<Path> parsed_;
    Ref<Array> bound_;                // the array holding the value; null when unbound
    uint32_t first_ = 0, count_ = 0;  // the value's nodes within bound_
    std::unique_ptr<std::vector<std::function<void()>>> on_change_;
};

// The set of live tweaks, as an intrusive list: adding one allocates nothing. The global
// registry is constant-initialized, so tweaks may register from any static initializer.
class TweakRegistry {
public:
    constexpr TweakRegistry() noexcept = default;
    TweakRegistry(const TweakRegistry&) = delete;
    TweakRegistry& operator=(const TweakRegistry&) = delete;

    [[nodiscard]] static TweakRegistry& global() noexcept;

    // Visits every registered tweak. `f` must not create or destroy tweaks.
    template <class F>
    void each(F&& f) const {
        lock();
        for (TweakBase* t = head_; t; t = t->next_) f(*t);
        unlock();
    }
    [[nodiscard]] size_t size() const noexcept;
    // The Config that binds these tweaks, or null.
    [[nodiscard]] Config* config() const noexcept { return config_; }

private:
    friend class TweakBase;
    friend class Config;
    void lock() const noexcept;
    void unlock() const noexcept;

    mutable std::atomic_flag lock_;
    TweakBase* head_ = nullptr;
    Config* config_ = nullptr;
};

namespace detail {
template <class T>
[[nodiscard]] constexpr bool atomic_tweak() noexcept {
    if constexpr (std::is_trivially_copyable_v<T>)
        return std::atomic<T>::is_always_lock_free;
    else
        return false;
}
}  // namespace detail

template <class T>
class Tweak final : public TweakBase {
    static constexpr bool kAtomic = detail::atomic_tweak<T>();

public:
    Tweak(const char* path, T fallback, const TweakMeta& meta = {},
          TweakRegistry& registry = TweakRegistry::global())
        : TweakBase(path, meta, registry), default_(fallback), value_(default_) {
        attach();
    }
    ~Tweak() { detach(); }

    operator T() const noexcept {  // NOLINT(google-explicit-constructor)
        if constexpr (kAtomic)
            return value_.load(std::memory_order_relaxed);
        else
            return value_;
    }
    [[nodiscard]] T value() const noexcept { return *this; }
    // For a T that is not read atomically: valid until the next reload.
    [[nodiscard]] const T& get() const noexcept
        requires(!kAtomic)
    {
        return value_;
    }
    [[nodiscard]] const T& default_value() const noexcept { return default_; }

    // A live edit: through the Config, so it shows in source() as layer "live" and can be
    // written back; without a Config, the value changes directly. Refused under
    // MOIRA_SHIPPING. Needs Convert<T>::to.
    [[nodiscard]] Expected<void> set(const T& v) {
        ValueList list;
        Convert<T>::to(v, list);
        return set_values(list.nodes());
    }

    [[nodiscard]] std::vector<Node> values() const override {
        ValueList list;
        if constexpr (requires {
                          Convert<T>::to(std::declval<const T&>(), std::declval<ValueList&>());
                      })
            Convert<T>::to(value(), list);
        return std::move(list.nodes());
    }

private:
    bool assign(const Values& values, bool apply) override {
        T v{};
        if (!Convert<T>::from(values, v)) return false;
        if (apply) store(std::move(v));
        return true;
    }
    void reset() override { store(default_); }
    [[nodiscard]] detail::ConvertInfo info() const noexcept override {
        return detail::convert_info<T>();
    }
    void store(T v) {
        if constexpr (kAtomic)
            value_.store(v, std::memory_order_relaxed);
        else
            value_ = std::move(v);
    }

    T default_;
    std::conditional_t<kAtomic, std::atomic<T>, T> value_;
};

}  // namespace moira
