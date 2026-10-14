#pragma once

#include "common/containers.hpp"

// Lexical rules shared by the parser and the printer. Keeping them in one place is what
// makes printing round-trip: the printer quotes exactly what the lexer would misread.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace baker {
class Array;
class Node;
class Path;
}  // namespace baker

namespace baker::detail {

// At most about `max_chars` of the compact form of `node`, ending in "..." if cut. Safe on
// huge or heavily shared trees; for error messages (write.cpp).
[[nodiscard]] Str preview(const Node& node, size_t max_chars = 40);
// 'name', as messages name a symbol.
[[nodiscard]] Str quoted(Symbol s);

// The text forms std_interop.hpp gives a host, for messages and edits inside Baker.
[[nodiscard]] Str text_of(const Path& path, size_t count = SIZE_MAX, char separator = '/');
[[nodiscard]] Str text_of(const Node& node);
[[nodiscard]] Str dta_of(const Array& root);

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

// Appends a path segment naming `name`: bare where a Path reads it back as that name,
// otherwise 'quoted'.
void append_path_name(Str& out, std::string_view name);

// Shortest text that reads back as exactly `v` (by classify_number), such as "12.75",
// "5", "1e+20" or "-0". NaN and infinities print as nan/inf/-inf. The caller adds ".0"
// where needed to keep integral values floats.
[[nodiscard]] Str format_double(double v);
// The portable implementation used where std::to_chars for doubles is unavailable (older
// Apple deployment targets); exposed for tests.
[[nodiscard]] Str format_double_fallback(double v);

// Lexical path handling. Baker never touches the file system itself (LoadSession's reader
// does), so include paths are only joined and normalized as text. Both '/' and '\\' are
// separators; results use '/'.
[[nodiscard]] bool is_absolute_path(std::string_view p) noexcept;
// Removes "." and empty segments and resolves ".." lexically: "a/./b/../c.dta" -> "a/c.dta".
[[nodiscard]] Str normalize_path(std::string_view p);
// Everything before the last separator: "a/b/c.dta" -> "a/b"; "c.dta" -> "".
[[nodiscard]] std::string_view parent_path(std::string_view p) noexcept;
[[nodiscard]] Str join_path(std::string_view dir, std::string_view p);

// `path`, terminated for fopen: on the stack unless it is long, so opening a file need not
// allocate.
class CPath {
public:
    explicit CPath(std::string_view path);
    [[nodiscard]] const char* c_str() const noexcept { return p_; }

private:
    char buf_[512];
    Str long_;
    const char* p_ = buf_;
};

}  // namespace baker::detail
