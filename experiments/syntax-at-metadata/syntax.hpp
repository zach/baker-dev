#pragma once

// The at-metadata form (dev/syntax-at-metadata-design.md): a tagged child whose tag begins
// with '@' is metadata, never a value and never a field:
//
//   (hit_pause 6 (@range 0 30) (@desc "Frames both fighters freeze"))
//   (move (@at fighters * moves) (@keyed) (startup int (@range 1 60) (@kind frames)))
//   {func boss.find_cover ($player) (@status stub) {nearest_marker ...}}
//
// The lexer, the printer and the tree are today's: '@' is a symbol character already, so
// the text parses as it stands and the tree holds (@range 0 30) as a tagged array. What
// changes is the reading of it (Array::values() ends at the first metadata child, with
// BAKER_AT_METADATA) and one rule of placement: metadata comes after the value.
//
// `print` is the migrator over a tree: today's metadata, named by D218's rule, the shape
// grammar's words and {func}'s (status)/(doc), get the sigil. `read` reads the form (the
// placement rule checked over read_structure, so it holds with the switch off too).
// `strip_sigils` is the inverse, for the round trip and `to-dta`.

#include <baker/array.hpp>
#include <baker/expected.hpp>

#include <string>
#include <string_view>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "at-metadata";
constexpr std::string_view kExtension = ".dta";  // the same lexer: a converted file is DTA

// Reads the form: Baker's reader, after the placement rule over the text's structure. Errors
// are located (file, line, column); nothing in the text can crash it.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Prints a root array in the form: each top-level node on its own line, metadata spelled
// with the sigil by the migration rules, and among one-line siblings the metadata in one
// column (whitespace only: the printer's option, not a rule of the form).
[[nodiscard]] std::string print(const Array& root);

// The tree with the sigil removed from every metadata tag: what the unmigrated text reads
// as. read() then strip_sigils() is the round trip's reader.
[[nodiscard]] Ref<Array> strip_sigils(const Array& root);
[[nodiscard]] Expected<Ref<Array>> read_as_dta(std::string_view text, std::string_view name);

// How many arrays in `root` are tagged with a symbol beginning with '@' (the D-entry's
// count of what would read as metadata).
[[nodiscard]] size_t count_meta_tags(const Array& root);

}  // namespace baker::experiments::syntax
