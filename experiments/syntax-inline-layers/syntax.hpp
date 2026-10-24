#pragma once

// Layers inside a file (dev/syntax-inline-layers-design.md). The new form is DTA with
// (#layer HEAD form*) blocks, which the core reads under BAKER_INLINE_LAYERS: a block's
// forms are lifted out of the file's tree into LoadSession::blocks(), and a Config pushes
// each as the sub-layer FILE:NAME above its file. So the reader and printer are Baker's own
// (a block never lives in a tree), and what this experiment adds is the converter between
// today's conditionals and blocks, and the check that a converted file merges to the same
// tree under every set of defines. It is a tool: Baker through its public headers, and the
// STL.

#include <baker/array.hpp>
#include <baker/expected.hpp>
#include <baker/parse.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "inline-layers";
constexpr std::string_view kExtension = ".dta";  // a converted file is still DTA

// The file's own tree, its blocks lifted out (baker::read_string with the core's switch on).
// Errors are located; nothing in the text can crash the reader.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// A tree as DTA. A block never lives in a tree, so this is baker::to_dta.
[[nodiscard]] std::string print(const Array& root);

// --- #ifdef regions to blocks and back ---

// One conditional region of a file, #ifdef or #ifndef N, an optional #else, #endif, and
// what the converter made of it.
struct Region {
    enum class Kind {
        Converted,       // now a block, or a block and the base's form
        SpansBracket,    // a branch opens or closes a bracket it does not close or open
        HoldsDirective,  // a branch holds a directive (#include, #define, another #ifdef)
        Nested,          // inside another region
        Values,          // a branch holds values, not only tagged forms
        Untagged,        // inside a command, a property, or an array with no tag yet
    };
    int line = 0;  // the #ifdef's
    std::string define;
    bool negated = false;  // #ifndef
    bool has_else = false;
    Kind kind = Kind::Converted;
};
[[nodiscard]] std::string_view describe(Region::Kind kind);

struct Conversion {
    std::string text;                 // the file, its convertible regions now blocks
    std::vector<Region> regions;      // every region found, in order
    std::vector<std::string> blocks;  // the blocks written, by name
};

// Converts each #ifdef/#ifndef region whose branches hold whole tagged forms, at the top
// level or inside tagged arrays, into a layer block. A block applies above its whole file,
// so the blocks go at the end of the file, one per condition, each branch wrapped in the
// tags that enclosed it. With an #else, the branch whose keys the other sets too stays in
// the file as the default and the other becomes the block; otherwise each branch is a block
// of its own, (when N) and (when (not N)). Comments and layout inside a branch are kept.
// Regions it cannot express are left as they are and reported.
[[nodiscard]] Conversion to_blocks(std::string_view text);

// A file's tree and the blocks its read collected, as DTA that today's Baker reads: the
// tree, then each block as #ifdef regions after it (a block applies above the whole file),
// its condition spelled with #ifdef, #ifndef and #else. A block with no condition is
// written as it is; a disabled one under #ifdef LAYER_<NAME>, which no host defines.
[[nodiscard]] std::string to_ifdefs(const Array& root, std::span<const Block> blocks);

// Every define a file's conditionals name, sorted: #ifdef and #ifndef, and (when ...) heads.
[[nodiscard]] std::vector<std::string> condition_defines(std::string_view text);

// What a Config merges a file to under `defines`, read from `text` as if it were the file
// at `path` (its includes from beside it). The merged root, or the error.
[[nodiscard]] Expected<Ref<Array>> merged(std::string_view path, std::string_view text,
                                          const std::vector<std::string>& defines);

// Whether two merged trees agree: "" when equal; "order" when they hold the same keys and
// values with keys in another order (a block's new keys follow the file's); else where
// they first differ.
[[nodiscard]] std::string compare_merged(const Array& a, const Array& b);

// The original file at `path` against its conversion `converted`, under every subset of
// the defines either names (up to 2^10 sets): one line per set that differs, empty if none.
// `order_only` counts the sets that agree only up to key order.
[[nodiscard]] std::vector<std::string> check_conversion(std::string_view path,
                                                        std::string_view original,
                                                        std::string_view converted,
                                                        int* sets = nullptr,
                                                        int* order_only = nullptr);

}  // namespace baker::experiments::syntax
