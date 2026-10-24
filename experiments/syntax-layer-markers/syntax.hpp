#pragma once

// Layer verbs beside #remove (dev/syntax-layer-markers-design.md). The text form is DTA:
// `#append`, `#drop` and `#was` are directives Baker's own parser reads in a layer row's
// slots (src/data/markers.hpp), so the reader is baker::read_string and the printer
// baker::to_dta. What this experiment adds is the conversion a package author or a tool
// would run: a layer that restates the base's lists, rewritten with the verbs against the
// base, and back.

#include <baker/array.hpp>
#include <baker/expected.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "layer-markers";
constexpr std::string_view kExtension = ".dta";  // the verbs are DTA

// Reads DTA with the verbs: baker::read_string.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Prints a root array as DTA; a verb prints bare in its slot, quoted elsewhere.
[[nodiscard]] std::string print(const Array& root);

// `layer`, which restates rows of `base` whole (what a package writes today), rewritten
// against `base` with the verbs, each row the first of these whose merge over `base` reads
// what the restated row says:
//   - a row equal to the base's is left out;
//   - a record (tagged children only) keeps its keyed children, each converted in turn;
//   - (key #append v...) when the base's run is a prefix of the row's;
//   - (key #drop v...) when the row's run is the base's with nodes taken out;
//   - (key v... #was w...) for a row with values, pinned to the base's values;
//   - the row as it was.
[[nodiscard]] Ref<Array> to_markers(const Array& base, const Array& layer);

// `layer` with its verbs resolved against `base`: each (key #append ...) or (key #drop ...)
// restated as the list the merge reads, and each #was pin left out. For a value list a
// Baker without the verbs reads it as this one reads `layer` over `base`; a list of keyed
// tuples restated cannot lose one by merging (#remove stays: it is D143's).
[[nodiscard]] Ref<Array> resolve(const Array& base, const Array& layer);

// Whether `layer` holds no verb or pin at any depth.
[[nodiscard]] bool marker_free(const Array& layer);

// One list in a stack's file: a key named like the stacks' lists (cancels, items, things,
// blocks, climbable, drops, glides, objectives), and the layer text a package writes to add
// one element and to take the first out, today and with a verb.
enum class ListKind {
    Values,        // (key v1 v2 ...): a layer restates it whole today
    Keyed,         // (key (a ...) (b ...)), every tag once: keyed children compose today
    KeyedRepeats,  // (key (bed ...) (bed ...)): a tag repeats, so a key reaches only the first
    OneValue,      // (key (e1 e2 ...)): the list is one value, which no verb reaches inside
};
struct ListRow {
    std::string path;  // dotted
    std::string file;  // where the list is written (an included file's own name)
    int line = 0;
    size_t elements = 0;
    bool one_value = false;
    ListKind kind = ListKind::Values;
    // Bytes of the layer's text, on one line: today's form (the list restated, or for a keyed
    // list the keyed child or its (tag #remove)) and the verb's.
    size_t add_before = 0, add_after = 0, drop_before = 0, drop_after = 0;
    bool add_same = false, drop_same = false;                // the verb does what was meant
    bool add_today_works = false, drop_today_works = false;  // today's form does
};
[[nodiscard]] std::vector<ListRow> list_rows(const Array& root);

}  // namespace baker::experiments::syntax
