#pragma once

// Ref<T>: intrusive reference-counted pointer.
//
// Refcounts are atomic by default, so Nodes and Refs may be copied and dropped on any
// thread; mutation is still single-threaded. Define MOIRA_ATOMIC_REFCOUNT=0 (CMake:
// -DMOIRA_ATOMIC_REFCOUNT=OFF) for plain counters in single-threaded programs
// (docs/decisions.md D20). Every translation unit must agree on the setting.
//
// A refcounted type T keeps a detail::RefCount as its first data member, is
// standard-layout, and provides ref() / unref() members.

#include <cstdint>
#include <utility>

#if !defined(MOIRA_ATOMIC_REFCOUNT) || MOIRA_ATOMIC_REFCOUNT
#define MOIRA_ATOMIC_REFCOUNT_ENABLED 1
#include <atomic>
#else
#define MOIRA_ATOMIC_REFCOUNT_ENABLED 0
#endif

namespace moira {

namespace detail {

#if MOIRA_ATOMIC_REFCOUNT_ENABLED
using RefCount = std::atomic<uint32_t>;
inline void retain(RefCount& c) noexcept {
    c.fetch_add(1, std::memory_order_relaxed);
}
// Returns true when the count reached zero and the object must be destroyed.
[[nodiscard]] inline bool release(RefCount& c) noexcept {
    return c.fetch_sub(1, std::memory_order_acq_rel) == 1;
}
[[nodiscard]] inline uint32_t count(const RefCount& c) noexcept {
    return c.load(std::memory_order_relaxed);
}
#else
using RefCount = uint32_t;
inline void retain(RefCount& c) noexcept {
    ++c;
}
[[nodiscard]] inline bool release(RefCount& c) noexcept {
    return --c == 0;
}
[[nodiscard]] inline uint32_t count(const RefCount& c) noexcept {
    return c;
}
#endif

// The count lives at offset 0 of every refcounted object (first member of a
// standard-layout class), so it can be reached from an untyped pointer.
[[nodiscard]] inline RefCount& count_of(const void* object) noexcept {
    return *static_cast<RefCount*>(const_cast<void*>(object));
}

}  // namespace detail

template <class T>
class Ref {
public:
    constexpr Ref() noexcept = default;
    constexpr Ref(std::nullptr_t) noexcept {}  // NOLINT(google-explicit-constructor)
    // Shares ownership of `p` (increments its count).
    explicit Ref(T* p) noexcept : p_(p) {
        if (p_) p_->ref();
    }
    // Takes over a reference the caller already owns (no increment).
    [[nodiscard]] static Ref adopt(T* p) noexcept {
        Ref r;
        r.p_ = p;
        return r;
    }

    Ref(const Ref& o) noexcept : p_(o.p_) {
        if (p_) p_->ref();
    }
    Ref(Ref&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    Ref& operator=(const Ref& o) noexcept {
        Ref(o).swap(*this);
        return *this;
    }
    Ref& operator=(Ref&& o) noexcept {
        Ref(std::move(o)).swap(*this);
        return *this;
    }
    ~Ref() {
        if (p_) p_->unref();
    }

    [[nodiscard]] T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    T& operator*() const noexcept { return *p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

    // Gives up ownership without decrementing.
    [[nodiscard]] T* release() noexcept { return std::exchange(p_, nullptr); }
    void reset() noexcept { Ref().swap(*this); }
    void swap(Ref& o) noexcept { std::swap(p_, o.p_); }

    friend bool operator==(const Ref& a, const Ref& b) noexcept { return a.p_ == b.p_; }
    friend bool operator==(const Ref& a, std::nullptr_t) noexcept { return a.p_ == nullptr; }

private:
    T* p_ = nullptr;
};

}  // namespace moira
