#pragma once

// Owners of arrays and strings (docs/decisions.md D64): solo blocks, and load arenas.

#include <moira/ref.hpp>

#include <cstddef>
#include <cstdint>

namespace moira::detail {

// A block holding one array or string: the owner pointer every block starts with, the
// owner itself, then the payload.
struct SoloBlock {
    Owner* owner;
    Owner self;
};
inline constexpr uint32_t kSoloOffset = sizeof(SoloBlock);

// Storage for a solo payload of `bytes`, from the default allocator; the payload's first
// member must be the offset kSoloOffset.
[[nodiscard]] void* solo_payload(OwnerKind kind, size_t bytes);

}  // namespace moira::detail
