#pragma once

// Lexical rules shared by the parser and the printer. Keeping them in one place is what
// makes printing round-trip: the printer quotes exactly what the lexer would misread.

#include <cstdint>
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
    int32_t i = 0;
    float f = 0;
};

// Classifies a bare atom: decimal or 0x-hex integer (optionally signed), decimal float,
// or not a number. Decimal ints must fit int32. Hex ints may use all 32 bits and keep
// their bit pattern (0xFFFFFFFF reads as -1). Floats must fit a float.
[[nodiscard]] Number classify_number(std::string_view atom) noexcept;

// True if `s` reads back as this symbol when written bare (unquoted).
[[nodiscard]] bool is_bare_symbol(std::string_view s) noexcept;

}  // namespace moira::detail
