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
#include <moira/text.hpp>

#include <string_view>

namespace moira {

struct WriteOptions {
    int indent = 2;  // spaces per nesting level
    int width = 80;  // arrays that fit within this many columns stay on one line
};

// Writes the compact single-line form of a node: (price 12.75).
void write(const Node& node, TextSink out);
// Writes the compact single-line form of an array, with parentheses.
void write(const Array& array, TextSink out);

// Writes the file form of a root array: each top-level node on its own line,
// pretty-printed, no enclosing parentheses. Reading it back gives a structurally equal
// tree. <moira/std_interop.hpp> has to_string(node) and to_dta(root), returning strings.
void write_dta(const Array& root, TextSink out, const WriteOptions& options = {});

[[nodiscard]] Expected<void> write_file(std::string_view path, const Array& root,
                                        const WriteOptions& options = {});

}  // namespace moira
