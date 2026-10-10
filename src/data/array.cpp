#include <moira/array.hpp>

#include <cstdlib>
#include <new>
#include <string>
#include <type_traits>

namespace moira {

static_assert(std::is_standard_layout_v<Array>);
static_assert(sizeof(Array) % alignof(Node) == 0, "nodes must follow the header aligned");

namespace {
uint16_t child_height(const Node& n) noexcept {
    const Array* a = n.array();
    return a ? a->height() : uint16_t{0};
}
uint16_t above(uint16_t h) noexcept {
    return h == UINT16_MAX ? h : static_cast<uint16_t>(h + 1);
}
}  // namespace

Array* Array::allocate(uint32_t size, Symbol file, int32_t line) {
    const size_t bytes = sizeof(Array) + static_cast<size_t>(size) * sizeof(Node);
    void* mem = ::operator new(bytes);
    return new (mem) Array(size, file, line);
}

Ref<Array> Array::make(uint32_t size, Symbol file, int32_t line) {
    Array* a = allocate(size, file, line);
    Node* d = a->data();
    for (uint32_t i = 0; i < size; ++i) new (d + i) Node();
    return Ref<Array>::adopt(a);
}

Ref<Array> Array::make_from(std::span<Node> nodes, Symbol file, int32_t line) {
    if (nodes.size() > UINT32_MAX) std::abort();
    const auto size = static_cast<uint32_t>(nodes.size());
    Array* a = allocate(size, file, line);
    Node* d = a->data();
    for (uint32_t i = 0; i < size; ++i) new (d + i) Node(std::move(nodes[i]));
    a->compute_height();
    return Ref<Array>::adopt(a);
}

Ref<Array> Array::make_copy(std::span<const Node> nodes, Symbol file, int32_t line) {
    if (nodes.size() > UINT32_MAX) std::abort();
    const auto size = static_cast<uint32_t>(nodes.size());
    Array* a = allocate(size, file, line);
    Node* d = a->data();
    for (uint32_t i = 0; i < size; ++i) new (d + i) Node(nodes[i]);
    a->compute_height();
    return Ref<Array>::adopt(a);
}

void Array::destroy(const Array* a) noexcept {
    auto* self = const_cast<Array*>(a);
    Node* d = self->data();
    for (uint32_t i = 0; i < self->size_; ++i) d[i].~Node();
    self->~Array();
    ::operator delete(self);
}

void Array::compute_height() noexcept {
    uint16_t h = 0;
    for (const Node& n : nodes()) {
        const uint16_t c = child_height(n);
        if (c > h) h = c;
    }
    height_ = above(h);
}

void Array::set(size_t i, Node node) noexcept {
    assert(i < size_);
    const uint16_t h = above(child_height(node));
    if (h > height_) height_ = h;
    data()[i] = std::move(node);
}

#if MOIRA_EXCEPTIONS
const Node& Array::at(size_t i) const {
    if (i < size_) return data()[i];
    detail::raise(make_error(
        ErrorCode::Range,
        "index " + std::to_string(i) + " out of range for array of size " + std::to_string(size_),
        file_, line_));
}
#endif

bool structurally_equal(const Node& a, const Node& b) noexcept {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Type::Int: return *a.try_int() == *b.try_int();
        case Type::Float: return a.float_bits() == b.float_bits();
        case Type::Symbol: return *a.try_sym() == *b.try_sym();
        case Type::String: return a.string()->view() == b.string()->view();
        case Type::Array:
        case Type::Command:
        case Type::Property: return structurally_equal(*a.array(), *b.array());
        case Type::Var: return a.var_name() == b.var_name();
        case Type::Func: return a.func_record() == b.func_record();
        case Type::Object: return a.object_handle() == b.object_handle();
    }
    return false;
}

bool structurally_equal(const Array& a, const Array& b) noexcept {
    if (&a == &b) return true;
    if (a.size() != b.size()) return false;
    for (uint32_t i = 0; i < a.size(); ++i)
        if (!structurally_equal(a[i], b[i])) return false;
    return true;
}

}  // namespace moira
