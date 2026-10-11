#pragma once

// Error: the structured description of everything that can go wrong in Moira.
//
// Errors carry data, not prose: what kind of failure, a message, and where it happened
// (file, line, and the array path that led there). Formatting is the caller's business;
// to_string() gives the canonical one-line form:
//
//   moira: 'price' is float, not int  (menu.dta:3, at menu/'bacon and eggs')
//
// Every throwing API has a non-throwing twin returning Expected<T>. Under
// MOIRA_NO_EXCEPTIONS the throwing forms (and Exception) are compiled out.

#include <moira/expected.hpp>
#include <moira/symbol.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>

#if defined(MOIRA_NO_EXCEPTIONS) && MOIRA_NO_EXCEPTIONS
#define MOIRA_EXCEPTIONS 0
#else
#define MOIRA_EXCEPTIONS 1
#endif

namespace moira {

// MOIRA_SHIPPING: reload and live edits are inert, and dev_only tweaks keep their code
// defaults (config.hpp, tweak.hpp).
#if defined(MOIRA_SHIPPING) && MOIRA_SHIPPING
inline constexpr bool kShipping = true;
#else
inline constexpr bool kShipping = false;
#endif

enum class ErrorCode : uint8_t {
    Syntax,     // malformed text
    Range,      // a value out of its representable range
    Directive,  // a preprocessor directive used incorrectly
    Io,         // a file could not be read or written
    Limit,      // a safety limit (nesting, node count, includes) was exceeded
    Type,       // a value has the wrong type for how it was accessed
    NotFound,   // a lookup found nothing
    Refused,    // an operation was declined: write-back without one literal source, a
                // mutation of a frozen config
};

[[nodiscard]] std::string_view error_code_name(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Syntax;
    std::string message;
    Symbol file;       // empty when unknown
    int32_t line = 0;  // 0 when unknown
    std::string path;  // array path, e.g. menu/'bacon and eggs'; empty when unknown
};

[[nodiscard]] Error make_error(ErrorCode code, std::string message, Symbol file = {},
                               int32_t line = 0);

// "moira: <message>  (<file>:<line>, at <path>)", omitting whatever is unknown.
[[nodiscard]] std::string to_string(const Error& error);

template <class T>
using Expected = expected<T, Error>;
using Unexpected = unexpected<Error>;

#if MOIRA_EXCEPTIONS

class Exception : public std::exception {
public:
    explicit Exception(Error error);
    [[nodiscard]] const Error& error() const noexcept { return error_; }
    [[nodiscard]] const char* what() const noexcept override { return what_.c_str(); }

private:
    Error error_;
    std::string what_;
};

namespace detail {
[[noreturn]] void raise(Error error);
}  // namespace detail

#endif

#if !MOIRA_HAS_STD_EXPECTED
namespace detail {
template <class E>
[[noreturn]] void throw_bad_expected_access(const E& e) {
#if MOIRA_EXCEPTIONS
    if constexpr (std::is_same_v<E, Error>) {
        raise(e);
    } else {
        (void)e;
        std::abort();
    }
#else
    (void)e;
    std::abort();
#endif
}
}  // namespace detail
#endif

}  // namespace moira
