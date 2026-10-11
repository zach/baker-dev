#pragma once

// Opt-in interop with the C++ standard library, for hosts that use it.
//
// Moira's other public headers reach only language-support headers and the non-allocating
// vocabulary types (string_view, span, optional, expected; docs/decisions.md D55), so an
// engine that bans the STL can include them. This header is the exception, by name: it
// includes the standard containers, and nothing else in Moira includes it.

#include <moira/error.hpp>
#include <moira/path.hpp>
#include <moira/symbol.hpp>
#include <moira/text.hpp>
#include <moira/write.hpp>

#include <cstddef>
#include <functional>
#include <string>

template <>
struct std::hash<moira::Symbol> {
    size_t operator()(moira::Symbol s) const noexcept { return static_cast<size_t>(s.hash()); }
};

namespace moira {

// A TextOut that appends to `s`.
inline TextOut string_out(std::string& s) {
    return {[](void* u, std::string_view t) { static_cast<std::string*>(u)->append(t); }, &s};
}

// "{bob attack}  (boss.dta:4)"
inline std::string to_string(const StackEntry& entry) {
    std::string out;
    write(entry, string_out(out));
    return out;
}

// menu/'bacon and eggs', as Path::write writes it.
inline std::string to_string(const Path& path, size_t count = SIZE_MAX, char separator = '/') {
    std::string out;
    path.write(string_out(out), count, separator);
    return out;
}

// The compact single-line form of a node or an array: (price 12.75).
inline std::string to_string(const Node& node) {
    std::string out;
    write(node, string_out(out));
    return out;
}
inline std::string to_string(const Array& array) {
    std::string out;
    write(array, string_out(out));
    return out;
}

// The file form of a root array (write_dta).
inline std::string to_dta(const Array& root, const WriteOptions& options = {}) {
    std::string out;
    write_dta(root, string_out(out), options);
    return out;
}

}  // namespace moira
