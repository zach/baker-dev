#pragma once

// The one merge routine behind #merge (fill-missing) and Config layers (overwrite).

#include <baker/config.hpp>
#include <baker/source_map.hpp>

#include "common/containers.hpp"

namespace baker::detail {

class Arena;

// Merges the nodes of `src` from `src_first` into `dst` from `dst_first` under `policy`
// (see baker::merge). Arrays along the way are rebuilt, never mutated, because they may be
// shared. With a `map`, `spans` runs parallel to `dst` and is kept in step, and rebuilt
// arrays are recorded in the map. Rebuilt arrays go into `arena` if given: a #merge
// inside a load must not make solo arrays that point back into the arena being built,
// which would then count each other (D64). Returns whether `dst` changed.
bool merge_into(Vec<Node>& dst, Vec<SourceSpan>* spans, size_t dst_first, const Array& src,
                size_t src_first, MergePolicy policy, SourceMap* map, Arena* arena = nullptr);

}  // namespace baker::detail
