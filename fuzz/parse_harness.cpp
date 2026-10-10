#include "parse_harness.hpp"

#include <moira/parse.hpp>
#include <moira/write.hpp>

#include <algorithm>
#include <cstdint>

namespace moira::fuzz {
namespace {

// Walks the tree as if fully expanded, stopping when `budget` runs out. Macro sharing
// can make a small tree print exponentially large; such trees are skipped.
bool fits(const Node& n, uint64_t& budget) {
    if (budget == 0) return false;
    --budget;
    if (const Array* a = n.array())
        for (const Node& c : *a)
            if (!fits(c, budget)) return false;
    return true;
}

LoadSession make_session() {
    LoadSession s;
    s.set_reader([](const std::string&, std::string&) { return false; });
    s.limits.max_depth = 64;
    s.limits.max_nodes = 1u << 16;
    return s;
}

}  // namespace

bool check_parse(std::string_view data) {
    LoadSession s = make_session();
    auto first = try_read_string(data, "fuzz.dta", s);
    if (!first) {
        const Error& e = first.error();
        const auto lines = 1 + std::count(data.begin(), data.end(), '\n');
        return !e.message.empty() && e.file == Symbol("fuzz.dta") && e.line >= 1 && e.line <= lines;
    }
    uint64_t budget = 100000;
    for (const Node& n : **first)
        if (!fits(n, budget)) return true;

    const std::string printed = to_dta(**first);
    LoadSession s2 = make_session();
    auto second = try_read_string(printed, "printed.dta", s2);
    return second && structurally_equal(**first, **second);
}

}  // namespace moira::fuzz
