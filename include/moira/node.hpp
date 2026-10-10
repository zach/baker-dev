#pragma once

// Node: the 16-byte tagged value every Moira tree is made of.
//
//   Type      Syntax              Payload
//   Int       42  -7  0x1F        int32_t
//   Float     12.75  1e-3         float
//   Symbol    price  'odd sym'    interned Symbol
//   String    "bacon"             Ref<String>
//   Array     ( ... )             Ref<Array>
//   Command   { ... }             Ref<Array>; evaluated when read as a value (moira::script)
//   Property  [ ... ]             Ref<Array>; property-path lookup (moira::script)
//   Var       $name               variable name; bound to a slot by moira::script
//   Func      -                   native function record (moira::script)
//   Object    -                   32-bit salted object handle (moira::script)
//
// Numbers are deliberately 32-bit: it matches the source format and keeps the node at
// 16 bytes. as<int64_t>() and as<double>() widen on read; storage never widens.
//
// Typed accessors (as_int, as_float, ...) are checked: a mismatch throws an Exception
// (or, for the try_ forms, returns nothing). Int and Float coerce into each other where
// no information is lost: an int always reads as a float, and a float reads as an int
// only if it is integral and in range.

#include <moira/error.hpp>
#include <moira/ref.hpp>
#include <moira/symbol.hpp>

#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace moira {

class Array;

enum class Type : uint8_t {
    Int,
    Float,
    Symbol,
    String,
    Array,
    Command,
    Property,
    Var,
    Func,
    Object,
};

[[nodiscard]] std::string_view type_name(Type type) noexcept;

// Reference to an object owned by a moira::script Context: 16 bits of table index and 16
// bits of salt. Opaque to moira::data, which can store, copy, and compare it but never
// resolve it.
struct ObjectHandle {
    uint32_t bits = 0;

    [[nodiscard]] static constexpr ObjectHandle make(uint16_t index, uint16_t salt) noexcept {
        return {static_cast<uint32_t>(index) | (static_cast<uint32_t>(salt) << 16)};
    }
    [[nodiscard]] constexpr uint16_t index() const noexcept {
        return static_cast<uint16_t>(bits & 0xFFFFu);
    }
    [[nodiscard]] constexpr uint16_t salt() const noexcept {
        return static_cast<uint16_t>(bits >> 16);
    }
    friend constexpr bool operator==(ObjectHandle, ObjectHandle) noexcept = default;
};

// The part of a native function record moira::data needs: its name, so a Func node can be
// printed. moira::script's handler records extend this.
struct FuncRecord {
    Symbol name;
};

namespace detail {
// Frees a refcounted payload whose count reached zero.
void destroy_counted(const void* payload, Type type) noexcept;
[[nodiscard]] std::optional<int32_t> float_to_int(float f) noexcept;
}  // namespace detail

// Refcounted immutable string, stored in a single allocation.
class String {
public:
    [[nodiscard]] static Ref<String> make(std::string_view text);

    [[nodiscard]] std::string_view view() const noexcept { return {data(), size_}; }
    [[nodiscard]] const char* data() const noexcept {
        return reinterpret_cast<const char*>(this + 1);
    }
    [[nodiscard]] uint32_t size() const noexcept { return size_; }

    void ref() const noexcept { detail::retain(refs_); }
    void unref() const noexcept {
        if (detail::release(refs_)) destroy(this);
    }
    [[nodiscard]] uint32_t ref_count() const noexcept { return detail::count(refs_); }

    String(const String&) = delete;
    String& operator=(const String&) = delete;

private:
    explicit String(uint32_t size) noexcept : refs_(1), size_(size) {}
    ~String() = default;
    static void destroy(const String* s) noexcept;
    friend void detail::destroy_counted(const void*, Type) noexcept;

    mutable detail::RefCount refs_;
    uint32_t size_;
    // char data[size_ + 1] follows.
};

class Node {
public:
    Node() noexcept : type_(Type::Int) { value_.i = 0; }
    Node(int32_t v) noexcept : type_(Type::Int) { value_.i = v; }         // NOLINT
    Node(float v) noexcept : type_(Type::Float) { value_.f = v; }         // NOLINT
    Node(double v) noexcept : Node(static_cast<float>(v)) {}              // NOLINT
    Node(Symbol s) noexcept : type_(Type::Symbol) { value_.p = s.id(); }  // NOLINT
    Node(const Ref<String>& s) noexcept : Node(s.get()) {}                // NOLINT
    Node(Ref<String>&& s) noexcept : type_(Type::String) {                // NOLINT
        assert(s && "Node from null Ref<String>");
        value_.p = s.release();
    }
    // `kind` is Type::Array, Type::Command, or Type::Property. `a` must not be null.
    Node(const Ref<Array>& a, Type kind = Type::Array) noexcept  // NOLINT
        : type_(array_kind(kind)) {
        assert(a && "Node from null Ref<Array>");
        value_.p = a.get();
        detail::retain(detail::count_of(value_.p));
    }
    Node(Ref<Array>&& a, Type kind = Type::Array) noexcept : type_(array_kind(kind)) {  // NOLINT
        assert(a && "Node from null Ref<Array>");
        value_.p = a.release();
    }

    [[nodiscard]] static Node var(Symbol name) noexcept {
        Node n;
        n.type_ = Type::Var;
        n.value_.p = name.id();
        return n;
    }
    [[nodiscard]] static Node object(ObjectHandle h) noexcept {
        Node n;
        n.type_ = Type::Object;
        n.value_.u = h.bits;
        return n;
    }
    // `f` must outlive every node referring to it (function records are never freed).
    [[nodiscard]] static Node func(const FuncRecord* f) noexcept {
        Node n;
        n.type_ = Type::Func;
        n.value_.p = f;
        return n;
    }

    Node(const Node& o) noexcept
        : value_(o.value_), type_(o.type_), aux_{o.aux_[0], o.aux_[1], o.aux_[2]} {
        if (counted()) detail::retain(detail::count_of(value_.p));
    }
    Node(Node&& o) noexcept
        : value_(o.value_), type_(o.type_), aux_{o.aux_[0], o.aux_[1], o.aux_[2]} {
        o.type_ = Type::Int;
        o.value_.i = 0;
    }
    Node& operator=(const Node& o) noexcept {
        Node(o).swap(*this);
        return *this;
    }
    Node& operator=(Node&& o) noexcept {
        Node(std::move(o)).swap(*this);
        return *this;
    }
    ~Node() {
        if (counted() && detail::release(detail::count_of(value_.p)))
            detail::destroy_counted(value_.p, type_);
    }
    void swap(Node& o) noexcept {
        std::swap(value_, o.value_);
        std::swap(type_, o.type_);
        std::swap(aux_, o.aux_);
    }

    [[nodiscard]] Type type() const noexcept { return type_; }
    // Array, Command, or Property.
    [[nodiscard]] bool is_array_like() const noexcept {
        return type_ == Type::Array || type_ == Type::Command || type_ == Type::Property;
    }
    [[nodiscard]] bool is_number() const noexcept {
        return type_ == Type::Int || type_ == Type::Float;
    }

    // --- Checked typed accessors (throwing). ---
#if MOIRA_EXCEPTIONS
    [[nodiscard]] int32_t as_int() const {
        if (auto v = try_int()) return *v;
        type_error(Type::Int);
    }
    [[nodiscard]] float as_float() const {
        if (auto v = try_float()) return *v;
        type_error(Type::Float);
    }
    [[nodiscard]] Symbol as_sym() const {
        if (type_ == Type::Symbol) return Symbol::from_id(value_.p);
        type_error(Type::Symbol);
    }
    [[nodiscard]] std::string_view as_str() const {
        if (type_ == Type::String) return string_payload()->view();
        type_error(Type::String);
    }
    [[nodiscard]] const Array& as_array() const {
        if (type_ == Type::Array) return *static_cast<const Array*>(value_.p);
        type_error(Type::Array);
    }
    // Widening reads: as<int64_t>(), as<double>(), and narrower integer types with a
    // range check.
    template <class T>
    [[nodiscard]] T as() const {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>,
                      "Node::as<T> supports integer and floating-point types");
        if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(as_float());
        } else {
            int32_t v = as_int();
            if (!std::in_range<T>(v)) range_error(v);
            return static_cast<T>(v);
        }
    }
#endif

    // --- Non-throwing mirrors. ---
    [[nodiscard]] std::optional<int32_t> try_int() const noexcept {
        if (type_ == Type::Int) return value_.i;
        if (type_ == Type::Float) return detail::float_to_int(value_.f);
        return std::nullopt;
    }
    [[nodiscard]] std::optional<float> try_float() const noexcept {
        if (type_ == Type::Float) return value_.f;
        if (type_ == Type::Int) return static_cast<float>(value_.i);
        return std::nullopt;
    }
    [[nodiscard]] std::optional<Symbol> try_sym() const noexcept {
        if (type_ == Type::Symbol) return Symbol::from_id(value_.p);
        return std::nullopt;
    }
    [[nodiscard]] std::optional<std::string_view> try_str() const noexcept {
        if (type_ == Type::String) return string_payload()->view();
        return std::nullopt;
    }
    [[nodiscard]] const Array* try_array() const noexcept {
        return type_ == Type::Array ? static_cast<const Array*>(value_.p) : nullptr;
    }

    // --- Raw payload access: no coercion, no evaluation. Wrong type gives null/empty. ---
    // The array behind an Array, Command, or Property node.
    [[nodiscard]] const Array* array() const noexcept {
        return is_array_like() ? static_cast<const Array*>(value_.p) : nullptr;
    }
    [[nodiscard]] Array* mutable_array() const noexcept {
        return is_array_like() ? static_cast<Array*>(const_cast<void*>(value_.p)) : nullptr;
    }
    [[nodiscard]] const String* string() const noexcept {
        return type_ == Type::String ? string_payload() : nullptr;
    }
    [[nodiscard]] Symbol var_name() const noexcept {
        return type_ == Type::Var ? Symbol::from_id(value_.p) : Symbol();
    }
    [[nodiscard]] ObjectHandle object_handle() const noexcept {
        return type_ == Type::Object ? ObjectHandle{value_.u} : ObjectHandle{};
    }
    [[nodiscard]] const FuncRecord* func_record() const noexcept {
        return type_ == Type::Func ? static_cast<const FuncRecord*>(value_.p) : nullptr;
    }
    // Bit pattern of a Float node (0 otherwise); for exact comparisons.
    [[nodiscard]] uint32_t float_bits() const noexcept {
        return type_ == Type::Float ? std::bit_cast<uint32_t>(value_.f) : 0;
    }

    // 24 spare bits, reserved for moira::script (resolved-handler generation; see
    // docs/decisions.md D5). Copied with the node; ignored by equality and printing.
    [[nodiscard]] uint32_t aux() const noexcept {
        return static_cast<uint32_t>(aux_[0]) | (static_cast<uint32_t>(aux_[1]) << 8) |
               (static_cast<uint32_t>(aux_[2]) << 16);
    }
    void set_aux(uint32_t v) noexcept {
        aux_[0] = static_cast<uint8_t>(v);
        aux_[1] = static_cast<uint8_t>(v >> 8);
        aux_[2] = static_cast<uint8_t>(v >> 16);
    }

private:
    [[nodiscard]] static constexpr Type array_kind(Type k) noexcept {
        return k == Type::Command || k == Type::Property ? k : Type::Array;
    }
    [[nodiscard]] bool counted() const noexcept {
        constexpr uint32_t kMask =
            (1u << static_cast<int>(Type::String)) | (1u << static_cast<int>(Type::Array)) |
            (1u << static_cast<int>(Type::Command)) | (1u << static_cast<int>(Type::Property));
        return ((1u << static_cast<int>(type_)) & kMask) != 0;
    }
    [[nodiscard]] const String* string_payload() const noexcept {
        return static_cast<const String*>(value_.p);
    }
    explicit Node(const String* s) noexcept : type_(Type::String) {
        assert(s && "Node from null Ref<String>");
        value_.p = s;
        detail::retain(detail::count_of(value_.p));
    }
#if MOIRA_EXCEPTIONS
    [[noreturn]] void type_error(Type expected) const;
    [[noreturn]] void range_error(int32_t v) const;
#endif

    union Value {
        int32_t i;
        float f;
        uint32_t u;
        const void* p;
    } value_;
    Type type_;
    uint8_t aux_[3] = {0, 0, 0};
    // 4 more bytes of padding remain; do not spend them without a reason.
};

static_assert(sizeof(Node) == 16, "Node must stay 16 bytes");
static_assert(alignof(Node) == 8, "Node must be 8-byte aligned");

}  // namespace moira
