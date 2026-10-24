#pragma once

// The hash-declarations experiment (dev/syntax-hash-declarations-design.md): what a file
// says to the reader moves into the `#` namespace. `#pack (NAME ...)`, `#origin ("src" ...)`
// and `#shape (NAME ...)` are directives taking one form; `[NAME ...]` is the one macro form
// and `#define NAME` only a flag; `#` opens a directive only before an ASCII letter, so `#1`,
// `#00aa00` and `#$%&` are plain symbols.
//
// The prototype is a front end over Baker's reader: `read` rewrites the new text into the
// DTA Baker reads today (a declaration becomes the first form, a `#shape` run becomes a
// `(shapes ...)` container, a `#`-led symbol is quoted) and hands it to baker::read_string;
// `print` writes a tree in the new form; `migrate` rewrites today's DTA text into the new
// form, keeping comments and layout. It is a tool: public headers and the STL.
//
// Built with BAKER_HASH_DECLARATIONS (CMake), the core reads the new form itself, and the
// front half is the identity: translate() returns the text, read_with() is read_string,
// print() is the printer without its (shapes ...) split, and check_received() is the core's
// check_layer_text. The migrator is the same in both builds; the CLI's core-record and
// core-check compare the two builds' reads of the corpus.

#include <baker/array.hpp>
#include <baker/config.hpp>
#include <baker/error.hpp>
#include <baker/expected.hpp>
#include <baker/parse.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "hash-declarations";
constexpr std::string_view kExtension = ".dta";  // the same files, migrated in place

// Reads the experiment's text form. Errors are located (file, line, column) and nothing
// in the text can crash the reader.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Prints a root array in the experiment's text form: each top-level node on its own line,
// a top-level (shapes ...) container as one `#shape (...)` per shape, and a symbol such as
// `#1` bare.
[[nodiscard]] std::string print(const Array& root);

// --- The pieces, for the CLI and the tests ---

// The DTA text Baker reads for `text` in the new form, or a located error. `notes` gets one
// line per legacy form the text still uses (a top-level (shapes ...), a first-form
// (pack ...) or (origin "..." ...)), which `check` would warn about once adopted.
// `data_first` is set when the text's first form is a (pack ...) or (origin "...") that is
// data: the translation then puts a node before it, which read_with drops.
[[nodiscard]] Expected<std::string> translate(std::string_view text, std::string_view name,
                                              std::vector<std::string>* notes = nullptr,
                                              bool* data_first = nullptr);

// `read`, through `session` (its pack levels, reader, include dirs), so a caller can see
// the packs and origins the text declared.
[[nodiscard]] Expected<Ref<Array>> read_with(std::string_view text, std::string_view name,
                                             LoadSession& session,
                                             std::vector<std::string>* notes = nullptr);

// Config::check_layer_text for received text in the new form, with the LayerPolicy
// classification the adopted reader would make: a #shape is a declaration, refused (located,
// ErrorCode::Refused) under data_only or scalars_only and admitted otherwise; #pack and
// #origin are admitted, as a published overlay declares itself with them (D162).
[[nodiscard]] Expected<LayerReport> check_received(const Config& cfg, std::string_view name,
                                                   std::string_view text,
                                                   const LayerPolicy& policy);

// A session as the corpus is read: the stacks' and examples' pack levels.
[[nodiscard]] LoadSession corpus_session();

// What `migrate` rewrote in one text.
struct Migration {
    int packs = 0;          // (pack ...) first forms -> #pack (...)
    int origins = 0;        // (origin "..." ...) declarations -> #origin (...)
    int shape_sets = 0;     // top-level (shapes ...) containers split
    int shapes = 0;         // #shape declarations written
    int define_bodies = 0;  // #define N (body) -> [N body]
    [[nodiscard]] int total() const noexcept {
        return packs + origins + shape_sets + define_bodies;
    }
};

// Today's DTA text in the new form: prefix-and-split, keeping every comment, blank line and
// the layout of what it does not touch. Text that does not parse is returned as it is.
[[nodiscard]] std::string migrate(std::string_view dta, Migration* report = nullptr);

// Installs on `session` a reader that migrates each file it serves and translates it back,
// so reading a file of today's corpus through it is the textual round trip (CLI
// `migrate-check`).
void serve_migrated(LoadSession& session, std::vector<Migration>* reports = nullptr);

}  // namespace baker::experiments::syntax
