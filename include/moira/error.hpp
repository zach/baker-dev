#pragma once

// Error: the structured description of everything that can go wrong in Moira.
//
// Errors carry data, not prose: what kind of failure, a message, and where it happened
// (file, line, and the array path that led there). Formatting is the caller's business;
// to_string() gives the canonical one-line form:
//
//   moira: 'price' is float, not int  (menu.dta:3, at menu/'bacon and eggs')
//
// An error from a script also carries the script call stack it unwound through
// (Error::stack); describe() renders it, one frame a line, innermost first.
//
// Moira never throws (docs/decisions.md D54): every fallible API returns Expected<T>, and the
// library is built with exceptions disabled.

#include <moira/expected.hpp>
#include <moira/symbol.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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

// One call on the script call stack: {on function ...} written at file:line. `on` is the
// object a method was sent to, when it has a name; empty for a function.
struct StackEntry {
    Symbol function;
    Symbol on;
    Symbol file;
    int32_t line = 0;
};

struct Error {
    ErrorCode code = ErrorCode::Syntax;
    std::string message;
    Symbol file;                    // empty when unknown
    int32_t line = 0;               // 0 when unknown
    std::string path;               // array path, e.g. menu/'bacon and eggs'; empty when unknown
    std::vector<StackEntry> stack;  // the script calls it unwound through, innermost first
};

[[nodiscard]] Error make_error(ErrorCode code, std::string message, Symbol file = {},
                               int32_t line = 0);

// "moira: <message>  (<file>:<line>, at <path>)", omitting whatever is unknown.
[[nodiscard]] std::string to_string(const Error& error);
// to_string, then a line per stack entry, innermost first:
//   moira: '%' by zero  (boss.dta:9)
//     in {%}  (boss.dta:9)
//     in {bob attack}  (boss.dta:4)
[[nodiscard]] std::string describe(const Error& error);
// "{bob attack}  (boss.dta:4)"
[[nodiscard]] std::string to_string(const StackEntry& entry);

template <class T>
using Expected = expected<T, Error>;
using Unexpected = unexpected<Error>;

#if !MOIRA_HAS_STD_EXPECTED
namespace detail {
// Reading the value of an Expected that holds an error is a bug in the caller: abort.
template <class E>
[[noreturn]] void throw_bad_expected_access(const E&) {
    std::abort();
}
}  // namespace detail
#endif

}  // namespace moira
