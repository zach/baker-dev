#include <moira/allocator.hpp>
#include <moira/array.hpp>
#include <moira/node.hpp>

#include "arena.hpp"

#include <new>

namespace moira::detail {

static_assert(sizeof(SoloBlock) == 16 && alignof(SoloBlock) <= alignof(Node));

void* solo_payload(OwnerKind kind, size_t bytes) {
    void* mem = default_alloc(sizeof(SoloBlock) + bytes, alignof(SoloBlock));
    auto* block = ::new (mem) SoloBlock{nullptr, {}};
    block->owner = &block->self;
    block->self.kind = kind;
    return block + 1;
}

namespace {
SoloBlock* solo_block(Owner& owner) noexcept {
    return reinterpret_cast<SoloBlock*>(reinterpret_cast<char*>(&owner) -
                                        offsetof(SoloBlock, self));
}
}  // namespace

void free_owner(Owner& owner) noexcept {
    switch (owner.kind) {
        case kSoloArray: {
            SoloBlock* block = solo_block(owner);
            auto* a = reinterpret_cast<Array*>(block + 1);
            const uint32_t size = a->size_;
            Node* d = a->data();
            for (uint32_t i = 0; i < size; ++i) d[i].~Node();
            a->~Array();
            block->~SoloBlock();
            default_free(block,
                         sizeof(SoloBlock) + sizeof(Array) + (size ? size : 1) * sizeof(Node));
            return;
        }
        case kSoloString: {
            SoloBlock* block = solo_block(owner);
            auto* s = reinterpret_cast<String*>(block + 1);
            const uint32_t size = s->size_;
            s->~String();
            block->~SoloBlock();
            default_free(block, sizeof(SoloBlock) + sizeof(String) + size + 1);
            return;
        }
        default: return;
    }
}

}  // namespace moira::detail
