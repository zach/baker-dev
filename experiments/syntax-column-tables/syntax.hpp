#pragma once

// Column tables (dev/syntax-column-tables-design.md): inside a parenthesized array, the
// directive `#columns (name ...)` declares the fields of the rows after it, and each row
// writes its key and then exactly one cell per column:
//
//   (goods
//      #columns (base_price demand spoil_days volatility)
//      (wheat    2          300    0          0.1)
//      (flour    10         120    60         _))          ; _ is a hole: no such field
//
// which is the tree (goods (wheat (base_price 2) (demand 300) ...) (flour ...)). A cell
// is one atom, one command, or one parenthesized array whose elements are the column's
// values, whatever its first element: (jab low_kick) under `cancels` is (cancels jab
// low_kick). Fewer cells than columns is a located error; nodes after the cells are the
// row's ordinary children, as written.
//
// The reader is a text rewrite: the tables are expanded into today's nested text, on the
// same lines, and baker::read_string reads that, so every directive, macro, pack and
// layer rule holds unchanged and every synthesized child carries its row's file and line.
// The printer writes a keyed table with a header where its heuristic says so (below),
// nested text otherwise, and round-trips. It is a tool: it uses the STL.

#include <baker/array.hpp>
#include <baker/expected.hpp>
#include <baker/parse.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "column-tables";
constexpr std::string_view kExtension = ".dta";  // the form is DTA plus one directive

// Reads the form. Errors are located (file, line, column) and nothing in the text can
// crash the reader. The two-argument form reads in a session of its own with the stacks'
// pack levels; the other reads in `session` (its include directories, macros, limits).
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name,
                                        LoadSession& session);

// The two-argument reader as one callable, for the harness.
[[nodiscard]] inline auto reader() {
    return [](std::string_view text, std::string_view name) { return read(text, name); };
}

// The rewrite alone: `text` with every headed table expanded into nested text, on the same
// lines. Text without `#columns` comes back unchanged. The reader's errors come from here.
[[nodiscard]] Expected<std::string> desugar(std::string_view text, std::string_view name);

struct PrintOptions {
    int indent = 3;
    int width = 96;             // arrays that fit within this many columns stay on one line
    bool tables = true;         // false: nested text everywhere (the core printer's form)
    bool align = true;          // pad cells into columns, and the header's names over them
    double min_coverage = 0.6;  // a table needs this share of its grid filled
};

// Prints a root array: each top-level node on its own line. A tagged array is printed as
// a headed table when every child is a keyed record of tagged fields, the fields of all
// rows admit one column order (no row writes a before b where another writes b before a),
// every field is flat (its values hold no array, or it is one command), no row repeats a
// field, the cells fill at least `min_coverage` of rows x columns, and every row fits
// the width on one line. A table of one row is not worth a header and stays nested.
[[nodiscard]] std::string print(const Array& root, const PrintOptions& options = {});

// What the heuristic finds in a tree, for the design note's numbers. A candidate is a
// tagged array of two or more keyed records whose children are all tagged arrays (the
// keyed table of D142); the rest say what became of each.
struct TableStats {
    size_t candidates = 0;
    size_t tables = 0;             // candidates the printer heads
    size_t rejected_order = 0;     // two rows disagree on the order of two fields
    size_t rejected_coverage = 0;  // too many holes
    size_t rejected_cell = 0;      // a field that is not flat, or a #remove marker
    size_t rejected_repeat = 0;    // a row repeats a field tag
    size_t rejected_width = 0;     // a row would not fit the width even unaligned
    size_t rows = 0;
    size_t columns = 0;  // header names written, over all tables
    size_t cells = 0;    // fields written as cells
    size_t holes = 0;
    long parens_removed = 0;   // 2 per atom or command cell; -2 per one-array cell
    size_t names_removed = 0;  // field names no longer written: the cells
};
[[nodiscard]] TableStats table_stats(const Array& root, const PrintOptions& options = {});

}  // namespace baker::experiments::syntax
