#pragma once

// The experiment's text form: a reader into Baker's tree and a printer out of it. The
// template is the identity (DTA in, DTA out); an experiment replaces syntax.cpp.

#include <baker/array.hpp>
#include <baker/expected.hpp>

#include <string>
#include <string_view>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "indent-lines";
constexpr std::string_view kExtension = ".dta";  // the form reads every bracketed file as today

// Reads the experiment's text form. Errors are located (file, line, column) and nothing
// in the text can crash the reader.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Prints a root array in the experiment's text form: each top-level node on its own line.
[[nodiscard]] std::string print(const Array& root);

// How many bare atoms first on their line at `text`'s top level the layout would make
// arrays of: the one way a bracketed file's meaning changes (the gate). Counted up to the
// first layout error.
[[nodiscard]] size_t top_level_lines(std::string_view text);

}  // namespace baker::experiments::syntax
