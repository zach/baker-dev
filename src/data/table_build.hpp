#pragma once

// A Config's baked tables (table.hpp, D292): the registered specs and their blocks, and the
// builder that makes a block from the merged tree.

#include <baker/table.hpp>

#include "common/containers.hpp"

namespace baker::detail {

struct TableSlot {
    explicit TableSlot(const Allocator& a) : at(a), sources(a), errors(a) {}
    TableSlot(TableSlot&&) noexcept = default;
    TableSlot& operator=(TableSlot&&) noexcept = default;
    TableSpec spec;
    Vec<Symbol> at;  // the shape's (@at ...) segments; `*` is any tag
    Ref<const BakedTable> built;
    // The arrays whose children are the records, in tree order, as of the last build: a
    // tree that shares them all has the same records.
    Vec<const Array*> sources;
    Vec<Error> errors;  // the last build's, as given to the warning handler
};

// Reads the spec's `at` into the slot; refused when it has no segment (a keyed shape is
// at a place).
[[nodiscard]] Expected<void> parse_table_at(const TableSpec& spec, TableSlot& slot);
// Whether `root` holds the same governing arrays the slot was built from.
[[nodiscard]] bool table_sources_unchanged(const TableSlot& slot, const Array& root,
                                           const Allocator& alloc);
// Builds the slot's table from `root`: one block from `alloc`; errors in records (a field
// that is not a literal, a wrong type) to `warn`, located, the member left at its default.
// Records the sources in the slot.
[[nodiscard]] Ref<const BakedTable> build_table(TableSlot& slot, const Array& root,
                                                const Allocator& alloc,
                                                const Callback<void(const Error&)>& warn);

}  // namespace baker::detail
