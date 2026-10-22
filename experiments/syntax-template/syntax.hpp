#pragma once

// The experiment's text form: a reader into Baker's tree and a printer out of it. The
// template is the identity (DTA in, DTA out); an experiment replaces syntax.cpp.

#include <baker/array.hpp>
#include <baker/expected.hpp>

#include <string>
#include <string_view>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "template";
constexpr std::string_view kExtension = ".dta";  // what a converted file is called

// Reads the experiment's text form. Errors are located (file, line, column) and nothing
// in the text can crash the reader.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Prints a root array in the experiment's text form: each top-level node on its own line.
[[nodiscard]] std::string print(const Array& root);

}  // namespace baker::experiments::syntax
