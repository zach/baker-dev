#pragma once

// Printing trees back to DTA text.
//
// The printer round-trips: parsing what it writes gives a structurally equal tree. It
// preserves nothing else; comments, formatting, and macro structure are gone (edit-mode
// parsing and write-back, M3, are how source text is preserved). Values that have no
// DTA spelling are listed in docs/decisions.md (D13).

#include <moira/array.hpp>
#include <moira/error.hpp>
#include <moira/node.hpp>

#include <string>
#include <string_view>

namespace moira {

struct WriteOptions {
    int indent = 2;  // spaces per nesting level
    int width = 80;  // arrays that fit within this many columns stay on one line
};

// Compact single-line form of a node: (price 12.75).
[[nodiscard]] std::string to_string(const Node& node);
// Compact single-line form of an array, with parentheses.
[[nodiscard]] std::string to_string(const Array& array);

// File form of a root array: each top-level node on its own line, pretty-printed, no
// enclosing parentheses. read_string(to_dta(root)) is structurally equal to root.
[[nodiscard]] std::string to_dta(const Array& root, const WriteOptions& options = {});

[[nodiscard]] Expected<void> write_file(std::string_view path, const Array& root,
                                        const WriteOptions& options = {});

namespace detail {
// At most about `max_chars` of the compact form, ending in "..." if cut. Safe on huge or
// heavily shared trees; for error messages.
[[nodiscard]] std::string preview(const Node& node, size_t max_chars = 40);
}  // namespace detail

}  // namespace moira
