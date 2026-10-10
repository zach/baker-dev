#pragma once

// moira::expected / moira::unexpected: std::expected when the standard library has it,
// otherwise a minimal shim with the subset of the interface Moira uses.

#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>
#define MOIRA_HAS_STD_EXPECTED 1
#else
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#define MOIRA_HAS_STD_EXPECTED 0
#endif

#include <cstdlib>

namespace moira {

#if MOIRA_HAS_STD_EXPECTED

template <class T, class E>
using expected = std::expected<T, E>;
template <class E>
using unexpected = std::unexpected<E>;

#else

template <class E>
class unexpected {
public:
    explicit unexpected(E e) : error_(std::move(e)) {}
    [[nodiscard]] const E& error() const& noexcept { return error_; }
    [[nodiscard]] E& error() & noexcept { return error_; }
    [[nodiscard]] E&& error() && noexcept { return std::move(error_); }

private:
    E error_;
};

namespace detail {
template <class E>
[[noreturn]] void throw_bad_expected_access(const E& e);
}  // namespace detail

template <class T, class E>
class expected {
public:
    using value_type = T;
    using error_type = E;

    expected() : v_(std::in_place_index<0>) {}
    expected(const T& v) : v_(std::in_place_index<0>, v) {}  // NOLINT(google-explicit-constructor)
    expected(T&& v) : v_(std::in_place_index<0>, std::move(v)) {}  // NOLINT
    template <class G>
    expected(unexpected<G> u)  // NOLINT(google-explicit-constructor)
        : v_(std::in_place_index<1>, std::move(u).error()) {}

    [[nodiscard]] bool has_value() const noexcept { return v_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& operator*() const& noexcept { return *std::get_if<0>(&v_); }
    [[nodiscard]] T& operator*() & noexcept { return *std::get_if<0>(&v_); }
    [[nodiscard]] T&& operator*() && noexcept { return std::move(*std::get_if<0>(&v_)); }
    [[nodiscard]] const T* operator->() const noexcept { return std::get_if<0>(&v_); }
    [[nodiscard]] T* operator->() noexcept { return std::get_if<0>(&v_); }

    [[nodiscard]] const T& value() const& {
        check();
        return **this;
    }
    [[nodiscard]] T& value() & {
        check();
        return **this;
    }
    [[nodiscard]] T&& value() && {
        check();
        return std::move(**this);
    }
    template <class U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return has_value() ? **this : static_cast<T>(std::forward<U>(fallback));
    }

    [[nodiscard]] const E& error() const& noexcept { return *std::get_if<1>(&v_); }
    [[nodiscard]] E& error() & noexcept { return *std::get_if<1>(&v_); }
    [[nodiscard]] E&& error() && noexcept { return std::move(*std::get_if<1>(&v_)); }

private:
    void check() const {
        if (!has_value()) detail::throw_bad_expected_access(error());
    }
    std::variant<T, E> v_;
};

template <class E>
class expected<void, E> {
public:
    using value_type = void;
    using error_type = E;

    expected() = default;
    template <class G>
    expected(unexpected<G> u) : e_(std::move(u).error()) {}  // NOLINT

    [[nodiscard]] bool has_value() const noexcept { return !e_.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }
    void value() const {
        if (e_) detail::throw_bad_expected_access(*e_);
    }
    [[nodiscard]] const E& error() const& noexcept { return *e_; }
    [[nodiscard]] E& error() & noexcept { return *e_; }

private:
    std::optional<E> e_;
};

#endif

}  // namespace moira
