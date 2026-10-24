#pragma once

// The layer verbs: `#remove` (D143), `#append`, `#drop`, and the `#was` pin
// (dev/syntax-layer-markers-design.md). Each is a reserved symbol the parser writes only
// where the slot rule allows it, in a tagged array's value run, and a Config's merge
// consumes them, so no merged tree holds one. This is the one place that knows their
// spellings and their forms: the parser, the printer, the merge and the layer tools all
// read them from here.

#include <baker/array.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace baker::detail {

enum class Marker : uint8_t { None, Remove, Append, Drop, Was };

[[nodiscard]] inline const Symbol& marker_symbol(Marker m) {
    static const Symbol symbols[] = {Symbol(), Symbol("#remove"), Symbol("#append"),
                                     Symbol("#drop"), Symbol("#was")};
    return symbols[static_cast<size_t>(m)];
}

// The marker a node is, or None: a type test, then at most four symbol compares.
[[nodiscard]] inline Marker marker_of(const Node& n) noexcept {
    if (n.type() != Type::Symbol) return Marker::None;
    for (uint8_t m = 1; m <= 4; ++m)
        if (n.is(marker_symbol(static_cast<Marker>(m)))) return static_cast<Marker>(m);
    return Marker::None;
}

// The marker the directive `#name` names, or None.
[[nodiscard]] inline Marker marker_named(std::string_view name) noexcept {
    for (uint8_t m = 1; m <= 4; ++m)
        if (marker_symbol(static_cast<Marker>(m)).str().substr(1) == name)
            return static_cast<Marker>(m);
    return Marker::None;
}

// The verb in a tagged array's first value slot: Remove, Append or Drop. None for a plain
// row, pinned or not, and for an array that holds no marker (one flag test).
[[nodiscard]] inline Marker verb_of(const Array& a) noexcept {
    if (!a.has_marker() || a.size() < 2 || !a[0].try_sym()) return Marker::None;
    const Marker m = marker_of(a[1]);
    return m == Marker::Was ? Marker::None : m;
}
[[nodiscard]] inline bool is_removal(const Array& a) noexcept {
    return verb_of(a) == Marker::Remove;
}
// Whether `a` changes the value below it without supplying one: (key #append ...) or
// (key #drop ...). Such an array is never a value's source.
[[nodiscard]] inline bool composes(const Array& a) noexcept {
    const Marker m = verb_of(a);
    return m == Marker::Append || m == Marker::Drop;
}

// Where #was stands in `nodes` (an array's, tag first), or nodes.size().
[[nodiscard]] inline size_t was_at(std::span<const Node> nodes) noexcept {
    for (size_t i = 1; i < nodes.size(); ++i)
        if (marker_of(nodes[i]) == Marker::Was) return i;
    return nodes.size();
}

// Whether `nodes` holds a marker after its first node. The parser knows when it pushed
// one; an array built in C++ is scanned once, in make_from, so the flag is the same.
[[nodiscard]] inline bool holds_marker(std::span<const Node> nodes) noexcept {
    // Only a tagged array holds one: the root, and any untagged array, are never scanned.
    for (size_t i = 1; i < nodes.size() && nodes[0].type() == Type::Symbol; ++i)
        if (marker_of(nodes[i]) != Marker::None) return true;
    return false;
}

// The form each marker is written in: the hint of its Directive error.
[[nodiscard]] inline const char* marker_hint(Marker m) noexcept {
    static const char* const hints[] = {
        "", "#remove is written as a key's only value: (key #remove), or (key #remove #was v...)",
        "#append is written as a key's first value, with what it adds: (key #append v...)",
        "#drop is written as a key's first value, with what it removes: (key #drop v...)",
        "#was is written after a key's values, with the values below: (key v... #was w...)"};
    return hints[static_cast<size_t>(m)];
}

// Whether `nodes` (an array's, tag first) holds its markers as the slot rule writes them:
// `#remove` alone after the tag, or followed by `#was` and what it removes; `#append` or
// `#drop` after the tag with one or more operands, which may be anything; `#was` after one
// or more values with one or more nodes after it, tagged arrays apart. Returns
// null, or the hint of the marker out of place. Shared by the parser, which refuses the
// array, and the printer, which quotes the marker where the parser would not take it.
[[nodiscard]] inline const char* marker_form_error(std::span<const Node> nodes) noexcept {
    const bool tagged = !nodes.empty() && nodes[0].try_sym().has_value();
    size_t values = 0, pins = 0;  // untagged nodes before and after #was
    Marker verb = Marker::None;
    bool was = false;
    for (size_t i = 1; i < nodes.size(); ++i) {
        const Marker m = marker_of(nodes[i]);
        if (m == Marker::None) {
            if (tag_of(nodes[i]).empty()) ++(was ? pins : values);
            continue;
        }
        if (!tagged) return marker_hint(m);
        if (m == Marker::Was) {
            if (was || verb == Marker::Append || verb == Marker::Drop ||
                (verb == Marker::None && values == 0))
                return marker_hint(m);
            was = true;
            continue;
        }
        if (i != 1) return marker_hint(m);
        verb = m;
    }
    if (verb == Marker::Remove && (values != 0 || (was ? pins == 0 : nodes.size() != 2)))
        return marker_hint(verb);
    if ((verb == Marker::Append || verb == Marker::Drop) && nodes.size() < 3)
        return marker_hint(verb);
    if (was && pins == 0) return marker_hint(Marker::Was);
    return nullptr;
}

}  // namespace baker::detail
