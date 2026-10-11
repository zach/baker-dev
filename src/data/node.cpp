#include <moira/array.hpp>
#include <moira/node.hpp>
#include <moira/write.hpp>

#include "text.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

namespace moira {

static_assert(std::is_standard_layout_v<String>);

std::string_view type_name(Type type) noexcept {
    switch (type) {
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Symbol: return "symbol";
        case Type::String: return "string";
        case Type::Array: return "array";
        case Type::Command: return "command";
        case Type::Property: return "property";
        case Type::Var: return "var";
        case Type::Func: return "func";
        case Type::Object: return "object";
    }
    return "unknown";
}

Ref<String> String::make(std::string_view text) {
    // A string this large cannot come from a parse (inputs are bounded long before).
    if (text.size() >= UINT32_MAX) std::abort();
    const auto size = static_cast<uint32_t>(text.size());
    void* mem = detail::default_alloc(sizeof(String) + size + 1, alignof(String));
    auto* s = new (mem) String(size);
    char* chars = reinterpret_cast<char*>(s + 1);
    if (size) std::memcpy(chars, text.data(), size);
    chars[size] = '\0';
    return Ref<String>::adopt(s);
}

void String::destroy(const String* s) noexcept {
    const size_t size = s->size();
    s->~String();
    detail::default_free(const_cast<String*>(s), sizeof(String) + size + 1);
}

namespace detail {

void destroy_counted(const void* payload, Type type) noexcept {
    if (type == Type::String)
        String::destroy(static_cast<const String*>(payload));
    else
        Array::destroy(static_cast<const Array*>(payload));
}

std::optional<int64_t> float_to_int(double f) noexcept {
    // [-2^63, 2^63) are exactly the doubles that convert to int64 without overflow. NaN
    // fails both comparisons.
    if (!(f >= -9223372036854775808.0 && f < 9223372036854775808.0)) return std::nullopt;
    const auto i = static_cast<int64_t>(f);
    if (static_cast<double>(i) != f) return std::nullopt;
    return i;
}

}  // namespace detail

Error Node::type_error(Type expected) const {
    const std::string text = detail::preview(*this);
    Error e = make_error(ErrorCode::Type, text + " is " + std::string(type_name(type_)) + ", not " +
                                              std::string(type_name(expected)));
    if (const auto* a = array()) {
        e.set_location(a->file(), a->line());
    }
    return e;
}

Error Node::range_error() const {
    return make_error(ErrorCode::Range,
                      detail::preview(*this) + " is out of range for the requested type");
}

}  // namespace moira
