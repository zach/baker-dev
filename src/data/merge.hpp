#pragma once

// The one merge routine behind #merge (fill-missing) and Config layers (overwrite).

#include <moira/config.hpp>
#include <moira/source_map.hpp>

#include <vector>

namespace moira::detail {

// Merges the nodes of `src` from `src_first` into `dst` from `dst_first` under `policy`
// (see moira::merge). Arrays along the way are rebuilt, never mutated, because they may be
// shared. With a `map`, `spans` runs parallel to `dst` and is kept in step, and rebuilt
// arrays are recorded in the map. Returns whether `dst` changed.
bool merge_into(std::vector<Node>& dst, std::vector<SourceSpan>* spans, size_t dst_first,
                const Array& src, size_t src_first, MergePolicy policy, SourceMap* map);

}  // namespace moira::detail
