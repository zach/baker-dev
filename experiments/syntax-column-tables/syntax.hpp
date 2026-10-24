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
// low_kick). Fewer cells than columns is a located error, and so is a list cell that
// begins with a column's bare name, (startup 4), which is a nested field written where a
// cell goes. Nodes after the cells are the row's ordinary children, as written, unless
// ReadOptions::trailing_children is off (the strict rule).
//
// The reader is a text rewrite: the tables are expanded into today's nested text, on the
// same lines, and baker::read_string reads that, so every directive, macro, pack and
// layer rule holds unchanged and every synthesized child carries its cell's file and line.
// The printer writes a keyed table with a header where its heuristic says so (below),
// nested text otherwise, and round-trips. HeadedFiles lets a Config read headed files and
// write back into them. It is a tool: it uses the STL.

#include <baker/array.hpp>
#include <baker/config.hpp>
#include <baker/expected.hpp>
#include <baker/parse.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "column-tables";
constexpr std::string_view kExtension = ".dta";  // the form is DTA plus one directive

struct ReadOptions {
    // The brief's rule (true): nodes after a row's N cells are its ordinary children. The
    // strict rule (false): a row is its key and exactly N cells, so a dropped or an extra
    // cell is always a count error. Under the brief's rule a dropped cell in a row that
    // carries children pulls the first child into the last column, which no count sees.
    bool trailing_children = true;
};

// Reads the form. Errors are located (file, line, column: a column in a rewritten row is
// mapped back to the text as written) and nothing in the text can crash the reader. The
// two-argument form reads in a session of its own whose reader rewrites included files
// too; the other reads in `session` (its include directories, macros, limits, reader).
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name,
                                        const ReadOptions& options = {});
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name,
                                        LoadSession& session, const ReadOptions& options = {});

// The two-argument reader as one callable, for the harness.
[[nodiscard]] inline auto reader() {
    return [](std::string_view text, std::string_view name) { return read(text, name); };
}

// The rewrite alone: `text` with every headed table expanded into nested text, on the same
// lines. Text without `#columns` comes back unchanged. The reader's errors come from here.
[[nodiscard]] Expected<std::string> desugar(std::string_view text, std::string_view name,
                                            const ReadOptions& options = {});

// Headed files under a Config, for write-back (D34): attach() sets the Config's session
// reader to rewrite each file it reads, and its writer to map what write-back writes into
// the rewritten text back onto the text as written. A changed value is the cell's own
// token; write_back_to of a field the row has a hole for fills the hole; of a field that
// is not a column, a child after the cells (refused under the strict rule); of a row the
// table lacks, a headed row with holes. Each write is checked: the headed text written
// must read to the tree the Config meant, or nothing is written. promote(), which writes
// many entries at once, is refused for a headed file. The object must outlive the Config.
class HeadedFiles {
public:
    // Reads a whole file into `out`, or returns false; writes a whole file.
    using Load = std::function<bool(std::string_view path, std::string& out)>;
    using Store = std::function<bool(std::string_view path, std::string_view text)>;

    explicit HeadedFiles(ReadOptions options = {});  // the local filesystem
    HeadedFiles(Load load, Store store, ReadOptions options = {});
    ~HeadedFiles();
    HeadedFiles(const HeadedFiles&) = delete;
    HeadedFiles& operator=(const HeadedFiles&) = delete;

    void attach(Config& config);
    [[nodiscard]] LoadSession::Reader reader();
    [[nodiscard]] Config::Writer writer();
    // Why the last read or write through this object failed: the Config's own error says
    // only that the file could not be read or written.
    [[nodiscard]] const std::optional<Error>& last_error() const noexcept { return error_; }
    void clear_error() noexcept { error_.reset(); }

private:
    struct File;
    bool load(std::string_view path, std::string& out);
    bool store(std::string_view path, std::string_view text);

    Load load_;
    Store store_;
    ReadOptions options_;
    std::map<std::string, std::unique_ptr<File>, std::less<>> files_;
    std::optional<Error> error_;
};

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
// the width on one line. A table of one row is not worth a header and stays nested. The
// printer writes every field as a cell, never a child after the cells, so its output
// reads the same under both rules.
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
    size_t list_cells = 0;         // cells written as a parenthesized list (they keep a pair)
    size_t list_symbol_first = 0;  // of those, lists that begin with a symbol: (jab low_kick)
    size_t list_column_first = 0;  // and that begin with a column's name, written quoted
    long parens_removed = 0;       // 2 per atom or command cell; a list cell keeps its pair
    size_t names_removed = 0;      // field names no longer written: the cells
    // Every candidate whose fields are all flat, whatever its order, coverage and width:
    // the upper bound a table syntax could reach in this tree.
    size_t flat_candidates = 0;
    size_t flat_rows = 0;
    size_t flat_fields = 0;
};
[[nodiscard]] TableStats table_stats(const Array& root, const PrintOptions& options = {});

}  // namespace baker::experiments::syntax
