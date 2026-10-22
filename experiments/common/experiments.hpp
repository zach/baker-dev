#pragma once

// What every syntax experiment shares (experiments/README.md).
//
// An experiment is a reader of some text form into Baker's existing tree, and a printer
// of the tree into that form. This library gives it the corpus to prove itself on, a
// comparison that names where two trees first differ, the round trip over the corpus, and
// a benchmark against Baker's own reader on the same content. It is a tool: it uses Baker
// through the public headers and the STL freely; the core's container rule (D55, D61) is
// for src/.

#include <baker/array.hpp>
#include <baker/error.hpp>
#include <baker/expected.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments {

// A reader of one text form into Baker's tree: what an experiment provides. `name` is the
// file name errors and provenance carry.
using Reader = std::function<Expected<Ref<Array>>(std::string_view text, std::string_view name)>;
// A printer of Baker's tree into one text form.
using Printer = std::function<std::string(const Array& root)>;

// The repository root, fixed at configure time.
[[nodiscard]] std::string_view repository_root();

// Every .dta file under the repository's corpus directories (tests/corpus, tests/shape,
// tests/host, examples, stacks), relative to the root, sorted. Build, run and local
// directories are left out.
[[nodiscard]] std::vector<std::string> corpus_files();

// A whole file, or an Io error.
[[nodiscard]] Expected<std::string> read_text_file(std::string_view path);

// Reads a corpus file as Baker reads it alone: one session, the stacks' and examples' pack levels,
// the file's own directory for its includes.
[[nodiscard]] Expected<Ref<Array>> read_dta_file(std::string_view path);

// Where two trees first differ: an index path from the root and both nodes' compact text.
// Empty when they are structurally equal (source locations are ignored).
[[nodiscard]] std::string first_difference(const Array& a, const Array& b);

// One corpus file that did not survive the round trip, and at which stage.
struct RoundTrip {
    enum class Stage {
        Baker,    // Baker's own reader refused the file (not the experiment's fault)
        Print,    // the printer refused it (returned empty text for a non-empty tree)
        Read,     // the experiment's reader refused the printed text
        Compare,  // the tree read back differs from the original
    };
    std::string file;  // relative to the repository root
    Stage stage;
    std::string problem;
};

// For each corpus file: Baker's reader -> `to_new` -> `from_new` -> compare with the
// original. Returns the files that failed; `skip` names files the experiment does not
// take yet (say why in the design note).
[[nodiscard]] std::vector<RoundTrip> round_trip_corpus(const Printer& to_new,
                                                       const Reader& from_new,
                                                       const std::vector<std::string>& skip = {});

// The failures that are the experiment's (every stage but Baker).
[[nodiscard]] std::vector<RoundTrip> experiment_faults(const std::vector<RoundTrip>& failures);

// Prints each failure as one line: "stage  file: problem".
void print_failures(const std::vector<RoundTrip>& failures);

// The parse benchmark's generated config-style text (bench/bench_parse.cpp), at least
// `bytes` long.
[[nodiscard]] std::string generated_config_text(size_t bytes);

struct BenchRow {
    std::string name;
    size_t bytes = 0;
    double best_ms = 0;
    double mb_per_s = 0;
};

// Times `read` over `text`, best of `runs`. A read that fails prints why and returns a
// row with mb_per_s 0.
[[nodiscard]] BenchRow bench_reader(std::string_view name, const Reader& read,
                                    std::string_view text, std::string_view file_name,
                                    int runs = 10);

// Baker's parser on `dta_text` against `read` on `converted_text` (the same content in the
// experiment's form), runs interleaved so both see the same machine state. Prints a table
// with each one's MB/s and the ratio of their best times, and returns the two rows.
std::pair<BenchRow, BenchRow> compare_parsers(const Reader& read, std::string_view dta_text,
                                              std::string_view converted_text, int rounds = 5);

}  // namespace baker::experiments
