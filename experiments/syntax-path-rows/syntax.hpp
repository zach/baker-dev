#pragma once

// Rows keyed by a path (dev/syntax-path-rows-design.md).
//
// A parenthesized array at a file's top level whose first atom is a bare symbol beginning
// with '/' is a ROW: `(/fighters.kestrel.moves.jab.startup 5)` is the tree
// `(fighters (kestrel (moves (jab (startup 5)))))`. The text after the '/' is a Path
// (D27): segments separated by '.' or '/', a 'quoted' segment for a name holding either.
// The row's body is an ordinary array body: values, metadata arrays, children, or
// #remove. At the root's close, when the file holds a row, siblings sharing a tag are
// folded into one array per tag, so each key appears once per level; a leaf written twice
// is a located error. Everything else is DTA as today, so every DTA file is a valid file
// of this form.
//
// This front end reads rows by parsing the text with Baker's own parser (the lexer takes
// `/a.b.c` as one symbol) and rewriting the tree: desugar each row, then fold. The design
// note describes the same rewrite inside the parser's close(), where the wrappers are
// arena arrays and the key token is tested as it is lexed. It is a tool: it uses Baker
// through the public headers and the STL.

#include <baker/array.hpp>
#include <baker/expected.hpp>

#include <string>
#include <string_view>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "path-rows";
constexpr std::string_view kExtension = ".dta";  // the form is DTA plus rows

// Reads the form: Baker's parser, then rows desugared and the root folded. Errors are
// located (file, line, and the key's column) and nothing in the text can crash the reader.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// The rewrite alone, on a tree Baker's parser made from `text` (named `name`): what the
// parser's close() would do. Exposed for the tests and the gate.
[[nodiscard]] Expected<Ref<Array>> rows_to_tree(Ref<Array> root, std::string_view text,
                                                std::string_view name);

// Where the printer could not write rows, and how much it wrote.
struct PrintStats {
    size_t rows = 0;               // rows written
    size_t verbatim_rows = 0;      // rows whose body kept nested children (order or duplicates)
    size_t nested_forms = 0;       // top-level forms written as plain DTA
    size_t repeated_top_tags = 0;  // files whose top level repeats a tag: all written nested
};

// Prints a root array as rows: one row per leaf path, in the tree's order, with D88's
// metadata arrays and bare tags kept in the row's body; a form rows cannot express is
// written as plain DTA. Reading the text back gives a structurally equal tree.
[[nodiscard]] std::string print(const Array& root);
[[nodiscard]] std::string print(const Array& root, PrintStats& stats);

// A tagged array's key text after the '/': `a.b.'v1.2'`; empty when a name cannot be a
// segment (a byte that ends an atom, a quote).
[[nodiscard]] std::string key_text(std::string_view name, bool first);

}  // namespace baker::experiments::syntax
