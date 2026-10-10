#include <moira/array.hpp>
#include <moira/node.hpp>
#include <moira/write.hpp>

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
    void* mem = ::operator new(sizeof(String) + size + 1);
    auto* s = new (mem) String(size);
    char* chars = reinterpret_cast<char*>(s + 1);
    if (size) std::memcpy(chars, text.data(), size);
    chars[size] = '\0';
    return Ref<String>::adopt(s);
}

void String::destroy(const String* s) noexcept {
    s->~String();
    ::operator delete(const_cast<String*>(s));
}

namespace detail {

void destroy_counted(const void* payload, Type type) noexcept {
    if (type == Type::String)
        String::destroy(static_cast<const String*>(payload));
    else
        Array::destroy(static_cast<const Array*>(payload));
}

std::optional<int32_t> float_to_int(float f) noexcept {
    // NaN fails both comparisons.
    if (!(f >= -2147483648.0f && f < 2147483648.0f)) return std::nullopt;
    const auto i = static_cast<int32_t>(f);
    if (static_cast<float>(i) != f) return std::nullopt;
    return i;
}

}  // namespace detail

#if MOIRA_EXCEPTIONS
void Node::type_error(Type expected) const {
    const std::string text = detail::preview(*this);
    const auto* a = array();
    Error e = make_error(ErrorCode::Type, text + " is " + std::string(type_name(type_)) + ", not " +
                                              std::string(type_name(expected)));
    if (a) {
        e.file = a->file();
        e.line = a->line();
    }
    detail::raise(std::move(e));
}

void Node::range_error(int32_t v) const {
    detail::raise(make_error(ErrorCode::Range, std::to_string(v) + " is out of range"));
}
#endif

}  // namespace moira
