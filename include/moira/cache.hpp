#pragma once

// The binary cache (spec §9, docs/decisions.md D70): a tree written as it lies in memory,
// and loaded with one read into one allocation and one pass that turns offsets into
// pointers.
//
//   auto root = moira::read_file("level1.dta", session);
//   moira::write_cache(**root, "level1.datb");
//   auto same = moira::read_cache("level1.datb", session);    // Expected<Ref<Array>>
//
// A cache holds the expanded tree: macros and #ifdef are resolved before it is written,
// and the session's #autorun commands are not part of it. Functions and objects, which
// moira::script makes and a parse never does, cannot be written. A cache is read only by
// a build of the same format version, endianness, and pointer size.
//
// The header's hash finds a damaged file, not a forged one: a cache is build output, so
// read text, which the parser checks, from a source you do not trust.

#include <moira/allocator.hpp>
#include <moira/array.hpp>
#include <moira/error.hpp>
#include <moira/parse.hpp>
#include <moira/text.hpp>

#include <string_view>

namespace moira {

[[nodiscard]] Expected<void> write_cache(const Array& root, std::string_view path);
// The same bytes, to `out`: for a host's own file system.
[[nodiscard]] Expected<void> write_cache(const Array& root, TextSink out);

// Through the session's reader and into its allocator. With the default reader, the tree
// is one allocation; a custom reader costs a temporary copy of the file.
[[nodiscard]] Expected<Ref<Array>> read_cache(std::string_view path, LoadSession& session);
[[nodiscard]] Expected<Ref<Array>> read_cache(std::string_view path);
// From a cache's bytes in memory, copied into the tree's one allocation. `name` labels
// errors; a null allocator is the default one.
[[nodiscard]] Expected<Ref<Array>> read_cache_bytes(std::string_view bytes,
                                                    std::string_view name = "<cache>",
                                                    const Allocator* allocator = nullptr);

}  // namespace moira
