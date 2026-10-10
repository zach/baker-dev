#pragma once

// Array: a refcounted, fixed-size sequence of Nodes stored in one allocation.
//
// The 24-byte header (refcount, size, source file, source line, height, flags) is
// followed directly by the nodes, so an array is one allocation and its children are
// contiguous. Every array remembers the file and line it was parsed from; that is what
// lets errors and provenance point at the source.
//
// at(i) returns the raw node. Typed lookup (find_int, find_float, ...) arrives in M2.

#include <moira/node.hpp>

#include <cassert>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace moira {

class Array {
public:
    // An array of `size` nodes, each Int 0.
    [[nodiscard]] static Ref<Array> make(uint32_t size, Symbol file = {}, int32_t line = 0);
    // An array holding `nodes`, which are moved from.
    [[nodiscard]] static Ref<Array> make_from(std::span<Node> nodes, Symbol file = {},
                                              int32_t line = 0);
    // An array holding copies of `nodes`.
    [[nodiscard]] static Ref<Array> make_copy(std::span<const Node> nodes, Symbol file = {},
                                              int32_t line = 0);

    [[nodiscard]] uint32_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

#if MOIRA_EXCEPTIONS
    // The raw node at `i`; never evaluates. Throws if `i` is out of range.
    [[nodiscard]] const Node& at(size_t i) const;
#endif
    // The raw node at `i`, or null if out of range.
    [[nodiscard]] const Node* try_at(size_t i) const noexcept {
        return i < size_ ? data() + i : nullptr;
    }
    // Unchecked (asserted) access.
    [[nodiscard]] const Node& operator[](size_t i) const noexcept {
        assert(i < size_);
        return data()[i];
    }

    [[nodiscard]] const Node* begin() const noexcept { return data(); }
    [[nodiscard]] const Node* end() const noexcept { return data() + size_; }
    [[nodiscard]] std::span<const Node> nodes() const noexcept { return {data(), size_}; }

    // Replaces the node at `i` (must be in range). Arrays may be shared, e.g. by macro
    // expansion, so the change is visible through every reference.
    void set(size_t i, Node node) noexcept;

    [[nodiscard]] Symbol file() const noexcept { return file_; }
    [[nodiscard]] int32_t line() const noexcept { return line_; }
    void set_source(Symbol file, int32_t line) noexcept {
        file_ = file;
        line_ = line;
    }

    // 1 + the height of the tallest child array; 1 for an array with no array children.
    // Bounds every recursive walk over a parsed tree. Saturates at 65535.
    [[nodiscard]] uint16_t height() const noexcept { return height_; }

    void ref() const noexcept { detail::retain(refs_); }
    void unref() const noexcept {
        if (detail::release(refs_)) destroy(this);
    }
    [[nodiscard]] uint32_t ref_count() const noexcept { return detail::count(refs_); }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

private:
    Array(uint32_t size, Symbol file, int32_t line) noexcept
        : refs_(1), size_(size), file_(file), line_(line) {}
    ~Array() = default;

    // Storage for `size` nodes, not yet constructed.
    [[nodiscard]] static Array* allocate(uint32_t size, Symbol file, int32_t line);
    static void destroy(const Array* a) noexcept;
    friend void detail::destroy_counted(const void*, Type) noexcept;
    void compute_height() noexcept;

    [[nodiscard]] Node* data() noexcept { return reinterpret_cast<Node*>(this + 1); }
    [[nodiscard]] const Node* data() const noexcept {
        return reinterpret_cast<const Node*>(this + 1);
    }

    mutable detail::RefCount refs_;
    uint32_t size_;
    Symbol file_;
    int32_t line_;
    uint16_t height_ = 1;
    uint16_t flags_ = 0;  // reserved (sorted-for-lookup mode, M2)
    // Node nodes[size_] follows.
};

static_assert(sizeof(Array) == 24, "Array header must stay 24 bytes");

// Deep comparison of type and value, ignoring source locations and Node::aux().
// Floats compare by bit pattern. Strings compare by content; arrays recursively.
[[nodiscard]] bool structurally_equal(const Node& a, const Node& b) noexcept;
[[nodiscard]] bool structurally_equal(const Array& a, const Array& b) noexcept;

// A refcounted string node payload: array("name", str("Bob")).
[[nodiscard]] inline Ref<String> str(std::string_view text) {
    return String::make(text);
}

namespace detail {
template <class T>
Node to_node(T&& v) {
    using D = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<D, Node>) {
        return std::forward<T>(v);
    } else if constexpr (std::is_convertible_v<const D&, std::string_view>) {
        return Node(Symbol(std::string_view(v)));
    } else {
        return Node(std::forward<T>(v));
    }
}
}  // namespace detail

// Builds an array from C++ values in one allocation sized exactly. Text becomes a
// Symbol; use str() for a String.
//
//   auto price = moira::array("price", 12.75f);   // (price 12.75)
//   auto eggs  = moira::array("eggs", price);     // (eggs (price 12.75))
template <class... Ts>
[[nodiscard]] Ref<Array> array(Ts&&... xs) {
    if constexpr (sizeof...(Ts) == 0) {
        return Array::make(0);
    } else {
        Node nodes[] = {detail::to_node(std::forward<Ts>(xs))...};
        return Array::make_from(nodes);
    }
}

}  // namespace moira
