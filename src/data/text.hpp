#pragma once

// Lexical rules shared by the parser and the printer. Keeping them in one place is what
// makes printing round-trip: the printer quotes exactly what the lexer would misread.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace moira::detail {

// Bytes that end an atom. Every byte <= 0x20 is whitespace.
[[nodiscard]] constexpr bool is_atom_end(unsigned char c) noexcept {
    return c <= 0x20 || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' ||
           c == '"' || c == ';';
}

enum class NumberKind : uint8_t { NotNumber, Int, Float, IntOutOfRange, FloatOutOfRange };

struct Number {
    NumberKind kind = NumberKind::NotNumber;
    int64_t i = 0;
    double f = 0;
};

// Classifies a bare atom: decimal or 0x-hex integer (optionally signed), decimal float,
// or not a number. Integers must fit int64; a hex literal denotes its unsigned value, so
// it must be at most 0x7FFFFFFFFFFFFFFF. Floats must be finite doubles (D18).
[[nodiscard]] Number classify_number(std::string_view atom) noexcept;

// True if `s` reads back as this symbol when written bare (unquoted).
[[nodiscard]] bool is_bare_symbol(std::string_view s) noexcept;

// Shortest text that reads back as exactly `v` (by classify_number), such as "12.75",
// "5", "1e+20" or "-0". NaN and infinities print as nan/inf/-inf. The caller adds ".0"
// where needed to keep integral values floats.
[[nodiscard]] std::string format_double(double v);
// The portable implementation used where std::to_chars for doubles is unavailable (older
// Apple deployment targets); exposed for tests.
[[nodiscard]] std::string format_double_fallback(double v);

}  // namespace moira::detail
