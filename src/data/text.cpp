#include "text.hpp"

#include <charconv>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

#include <fast_float.h>

// libc++ on Apple ships double std::to_chars only for recent deployment targets, so Apple
// builds use the portable formatter. Define MOIRA_FORCE_FORMAT_FALLBACK to use it anywhere.
#if defined(MOIRA_FORCE_FORMAT_FALLBACK) || (defined(__APPLE__) && defined(_LIBCPP_VERSION))
#define MOIRA_FORMAT_FALLBACK 1
#else
#define MOIRA_FORMAT_FALLBACK 0
#endif

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

// Locale-independent parse of an unsigned decimal float. fast_float is used everywhere,
// not just where std::from_chars lacks doubles (libc++ before LLVM 20), so every
// platform reads the same text as the same value.
bool parse_double(std::string_view s, double& out, bool& out_of_range) noexcept {
    const char* end = s.data() + s.size();
    auto [p, ec] = fast_float::from_chars(s.data(), end, out, fast_float::chars_format::general);
    out_of_range = ec == std::errc::result_out_of_range;
    return p == end && (ec == std::errc() || out_of_range);
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
                overflow = v > (uint64_t{INT64_MAX} >> 4);
                v = v * 16 + static_cast<uint64_t>(d);
                overflow = overflow || v > uint64_t{INT64_MAX};
            }
        }
        if (overflow) return {NumberKind::IntOutOfRange};
        const auto i = static_cast<int64_t>(v);
        return {NumberKind::Int, neg ? -i : i};
    }

    bool all_digits = true;
    for (char c : r) all_digits = all_digits && is_digit(c);
    if (all_digits) {
        // Accumulate negatively so INT64_MIN is representable.
        int64_t v = 0;
        for (char c : r) {
            const int d = c - '0';
            if (v < (INT64_MIN + d) / 10) return {NumberKind::IntOutOfRange};
            v = v * 10 - d;
        }
        if (!neg) {
            if (v == INT64_MIN) return {NumberKind::IntOutOfRange};
            v = -v;
        }
        return {NumberKind::Int, v};
    }

    if (is_digit(r[0]) || (r[0] == '.' && r.size() > 1 && is_digit(r[1]))) {
        double f = 0;
        bool out_of_range = false;
        if (!parse_double(r, f, out_of_range)) return {};
        if (out_of_range || !std::isfinite(f)) return {NumberKind::FloatOutOfRange};
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

std::string format_double_fallback(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    // The shortest %g precision that reads back exactly. %g follows the C locale's decimal
    // point, so normalize it.
    const char* point = std::localeconv()->decimal_point;
    const std::string_view decimal = point && *point ? point : ".";
    std::string text;
    for (int precision = 1; precision <= 17; ++precision) {
        char buf[64];
        const int n = std::snprintf(buf, sizeof buf, "%.*g", precision, v);
        text.assign(buf, n > 0 ? static_cast<size_t>(n) : 0);
        if (decimal != ".")
            for (size_t at = text.find(decimal); at != std::string::npos;
                 at = text.find(decimal, at + 1))
                text.replace(at, decimal.size(), ".");
        std::string_view digits = text;
        const bool neg = !digits.empty() && digits[0] == '-';
        if (neg) digits.remove_prefix(1);
        double back = 0;
        bool out_of_range = false;
        if (parse_double(digits, back, out_of_range) && (neg ? -back : back) == v) break;
    }
    return text;
}

std::string format_double(double v) {
#if MOIRA_FORMAT_FALLBACK
    return format_double_fallback(v);
#else
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
#endif
}

namespace {
bool is_separator(char c) noexcept {
    return c == '/' || c == '\\';
}
bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// "/", "C:/", or "C:" (a drive-relative prefix); 0 if none.
size_t root_length(std::string_view p) noexcept {
    if (p.size() >= 2 && is_alpha(p[0]) && p[1] == ':')
        return p.size() >= 3 && is_separator(p[2]) ? 3 : 2;
    return !p.empty() && is_separator(p[0]) ? 1 : 0;
}
}  // namespace

bool is_absolute_path(std::string_view p) noexcept {
    const size_t root = root_length(p);
    return root > 0 && is_separator(p[root - 1]);
}

std::string normalize_path(std::string_view p) {
    const size_t root = root_length(p);
    const bool absolute = is_absolute_path(p);
    std::vector<std::string_view> parts;
    size_t start = root;
    for (size_t i = root; i <= p.size(); ++i) {
        if (i < p.size() && !is_separator(p[i])) continue;
        const std::string_view part = p.substr(start, i - start);
        start = i + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!parts.empty() && parts.back() != "..")
                parts.pop_back();
            else if (!absolute)
                parts.push_back(part);
            continue;
        }
        parts.push_back(part);
    }
    std::string out(p.substr(0, root));
    for (char& c : out)
        if (c == '\\') c = '/';
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += '/';
        out += parts[i];
    }
    return out.empty() && !p.empty() ? "." : out;
}

std::string_view parent_path(std::string_view p) noexcept {
    size_t i = p.size();
    while (i > 0 && !is_separator(p[i - 1])) --i;
    if (i == 0) return {};
    return i == 1 ? p.substr(0, 1) : p.substr(0, i - 1);
}

std::string join_path(std::string_view dir, std::string_view p) {
    if (dir.empty()) return std::string(p);
    std::string out(dir);
    if (!is_separator(out.back())) out += '/';
    out += p;
    return out;
}

}  // namespace moira::detail
