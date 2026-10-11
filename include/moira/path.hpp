#pragma once

// Path: a parsed route through a tree, for Array::get and (M3) Config::get.
//
//   renderer.clear_color.0          name, name, index
//   menu.'bacon and eggs'.price     a quoted segment may hold any character but '
//   menu/'bacon and eggs'/price     '/' separates too, so a path copied from an error works
//
// A name segment selects the child array tagged with that symbol, and continues with the
// values after the tag. An all-digit segment selects the value at that index (0 is the
// first value after the tag); if that value is an array, the path continues inside it.
// The empty path is the array itself. A key whose name is all digits, or contains '.' or
// '/', must be quoted.
//
// Parsing interns each name. Build a Path once (a static or a member) for lookups in a
// hot loop; the string forms of get() parse on every call.

#include <moira/symbol.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace moira {

class Path {
public:
    struct Segment {
        Symbol name;         // when !is_index
        uint32_t index = 0;  // when is_index
        bool is_index = false;
    };

    Path() = default;
    Path(std::string_view text);                              // NOLINT(google-explicit-constructor)
    Path(const char* text) : Path(std::string_view(text)) {}  // NOLINT
    Path(const std::string& text) : Path(std::string_view(text)) {}  // NOLINT

    // False for malformed text (an empty segment, an unterminated quote, an index beyond
    // 32 bits). Lookups along an invalid path fail with ErrorCode::Syntax.
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] size_t size() const noexcept { return segments_.size(); }
    [[nodiscard]] bool empty() const noexcept { return segments_.empty(); }
    [[nodiscard]] const Segment& operator[](size_t i) const noexcept { return segments_[i]; }

    // The first `count` segments, '/'-separated and quoted where needed, as in errors:
    // menu/'bacon and eggs'. An invalid path gives the text it was parsed from.
    [[nodiscard]] std::string to_string(size_t count = SIZE_MAX) const;

private:
    bool parse(std::string_view text);

    std::vector<Segment> segments_;
    std::string bad_text_;  // only for an invalid path
    bool valid_ = true;
};

}  // namespace moira
