// Small words every stack wrote again (D115, stacks/audit.md): reading a game's structured
// data with fallbacks, and the errors and truth of its script words.
//
//   for (const baker::Array& a : kinds->subarrays()) {
//       k.name = base::name(a);                   // the array's tag
//       k.mass = base::number(a, "mass", 0);       // (mass 4), or 0
//       k.y = base::number(a, "pos.1", 0);         // the second value of (pos x y)
//   }
#pragma once

#include <baker/array.hpp>
#include <baker/error.hpp>
#include <baker/path.hpp>

#include <cstddef>
#include <string>

namespace base {

// An array's tag as a string; empty if it has none.
[[nodiscard]] inline std::string name(const baker::Array& a) {
    return std::string(a.tag().str());
}

// The number at `path` under `a` ("mass", or "pos.1" for a value by position), or
// `fallback` if it is absent or not a number. A mistyped value is the shapes' to report.
[[nodiscard]] inline float number(const baker::Array& a, const baker::Path& path, float fallback) {
    return static_cast<float>(a.get<double>(path, fallback).value_or(fallback));
}

// The value at position `i` of the child tagged `tag`: (size 2 0.5) has 2 at 0, 0.5 at 1;
// `fallback` if either is absent or not a number.
[[nodiscard]] inline float number(const baker::Array& a, const char* tag, size_t i,
                                  float fallback) {
    const baker::Array* f = a.try_find(baker::Symbol(tag));
    if (!f || f->size() <= i + 1) return fallback;
    return static_cast<float>((*f)[i + 1].as_float().value_or(fallback));
}

// The error a script word returns for a name the data does not have.
[[nodiscard]] inline baker::Error not_found(const std::string& what) {
    return baker::make_error(baker::ErrorCode::NotFound, what);
}

// A command's result as a goal's truth: a number is true if not 0; anything else is true.
[[nodiscard]] inline bool truthy(const baker::Node& n) {
    if (auto f = n.as_float()) return *f != 0;
    return true;
}

}  // namespace base
