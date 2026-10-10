#include "text.hpp"

#include <bit>
#include <charconv>
#include <system_error>

namespace moira::detail {
namespace {
bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

Number classify_number(std::string_view s) noexcept {
    std::string_view r = s;
    bool neg = false;
    if (!r.empty() && (r[0] == '+' || r[0] == '-')) {
        neg = r[0] == '-';
        r.remove_prefix(1);
    }
    if (r.empty()) return {};

    if (r.size() > 2 && r[0] == '0' && (r[1] == 'x' || r[1] == 'X')) {
        uint64_t v = 0;
        bool overflow = false;
        for (char c : r.substr(2)) {
            const int d = hex_value(c);
            if (d < 0) return {};
            if (!overflow) {
                v = v * 16 + static_cast<uint64_t>(d);
                overflow = v > 0xFFFFFFFFull;
            }
        }
        if (overflow) return {NumberKind::IntOutOfRange};
        uint32_t u = static_cast<uint32_t>(v);
        if (neg) u = 0u - u;
        return {NumberKind::Int, std::bit_cast<int32_t>(u)};
    }

    bool all_digits = true;
    for (char c : r) all_digits = all_digits && is_digit(c);
    if (all_digits) {
        int64_t v = 0;
        for (char c : r) {
            v = v * 10 + (c - '0');
            if (v > 2147483648ll) return {NumberKind::IntOutOfRange};
        }
        if (neg) v = -v;
        if (v > INT32_MAX || v < INT32_MIN) return {NumberKind::IntOutOfRange};
        return {NumberKind::Int, static_cast<int32_t>(v)};
    }

    if (is_digit(r[0]) || (r[0] == '.' && r.size() > 1 && is_digit(r[1]))) {
        float f = 0;
        const char* end = r.data() + r.size();
        auto [p, ec] = std::from_chars(r.data(), end, f, std::chars_format::general);
        if (p != end) return {};
        if (ec == std::errc::result_out_of_range) return {NumberKind::FloatOutOfRange};
        if (ec != std::errc()) return {};
        return {NumberKind::Float, 0, neg ? -f : f};
    }
    return {};
}

bool is_bare_symbol(std::string_view s) noexcept {
    if (s.empty()) return false;
    for (char c : s)
        if (is_atom_end(static_cast<unsigned char>(c))) return false;
    if (s[0] == '\'' || s[0] == '$' || s[0] == '#') return false;
    if (s.size() >= 2 && s[0] == '/' && s[1] == '*') return false;
    return classify_number(s).kind == NumberKind::NotNumber;
}

}  // namespace moira::detail
