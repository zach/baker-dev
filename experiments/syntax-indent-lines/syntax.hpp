#pragma once

// The experiment's text form: a reader into Baker's tree and a printer out of it. The
// template is the identity (DTA in, DTA out); an experiment replaces syntax.cpp.

#include <baker/array.hpp>
#include <baker/expected.hpp>

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

// How many bare atoms first on their line at `text`'s top level the layout would make
// arrays of: the one way a bracketed file's meaning changes (the gate). Counted up to the
// first layout error.
[[nodiscard]] size_t top_level_lines(std::string_view text);

}  // namespace baker::experiments::syntax
