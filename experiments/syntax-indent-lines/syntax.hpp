#pragma once

// The experiment's text form: a reader into Baker's tree and a printer out of it. The
// template is the identity (DTA in, DTA out); an experiment replaces syntax.cpp.

#include <baker/array.hpp>
#include <baker/expected.hpp>
#include <baker/parse.hpp>

#include <string>
#include <string_view>

// The core's switch (CMakeLists.txt): 0 reads today's text, 1 lines always, 2 lines from a
// file's #lines on.
#ifndef BAKER_INDENT_LINES
#define BAKER_INDENT_LINES 0
#endif

namespace baker::experiments::syntax {

constexpr std::string_view kName = "indent-lines";
constexpr std::string_view kExtension = ".dta";  // the form reads every bracketed file as today

// Reads the experiment's text form. Errors are located (file, line, column) and nothing
// in the text can crash the reader. In a build that reads lines (BAKER_INDENT_LINES) this
// is the core's reader; else the translator.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Prints a root array in the experiment's text form: the core's printer (WriteOptions::
// lines) in a build that reads lines, else the translator's.
[[nodiscard]] std::string print(const Array& root);

// The translator, in any build: the layout as a pass over the text that writes the
// brackets it implies, then Baker's reader of today's text.
[[nodiscard]] Expected<Ref<Array>> translate(std::string_view text, std::string_view name);
[[nodiscard]] std::string translator_print(const Array& root);

// read and translate with the caller's session (its pack levels, reader, includes).
[[nodiscard]] Expected<Ref<Array>> read_with(std::string_view text, std::string_view name,
                                             LoadSession& session);
[[nodiscard]] Expected<Ref<Array>> translate_with(std::string_view text, std::string_view name,
                                                  LoadSession& session);

// The migration tool (convert.cpp): today's text with the parentheses of each array the
// layout reads back as that array deleted, every comment and directive kept. The caller
// reads it back to check. A text Baker cannot read structurally is returned as it is.
struct Conversion {
    std::string text;
    size_t arrays = 0;         // parenthesized arrays in the text
    size_t first_on_line = 0;  // of them, those whose '(' is the first token on its line
    size_t lines = 0;          // of them, those made lines (two parentheses each go)
    // Why the rest of them keep their parentheses:
    size_t untagged = 0;      // no symbol tag on the '(' line (a tuple row), or a macro
    size_t in_brackets = 0;   // its parent keeps brackets
    size_t under_values = 0;  // its parent holds a value: (objective NAME (desc ...))
    size_t beside_tag = 0;    // it is in a row wrapped under the node after its parent's tag
    size_t continued = 0;     // a line inside it begins with a value, or at another indent
    size_t layout = 0;        // its indent is not its parent's body's, or a tab
    size_t shares_line = 0;   // a node of its parent follows its ')' on the line
};
[[nodiscard]] Conversion to_lines_text(std::string_view text);

// How many bare atoms first on their line at `text`'s top level the layout would make
// arrays of: the one way a bracketed file's meaning changes (the gate). Counted up to the
// first layout error.
[[nodiscard]] size_t top_level_lines(std::string_view text);

}  // namespace baker::experiments::syntax
