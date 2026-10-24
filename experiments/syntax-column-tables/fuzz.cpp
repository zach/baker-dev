// libFuzzer over the column-tables reader and printer, the property of fuzz/parse_harness.cpp
// for this form: a read is a located error, or a tree that prints and reads back to itself,
// under both row rules. Built with -DBAKER_BUILD_EXPERIMENTS=ON -DBAKER_BUILD_FUZZERS=ON
// (Clang), and run as the core's fuzzer is:
//   build/experiments/syntax-column-tables/baker_syntax_column-tables_fuzzer -dict=fuzz/dta.dict
#include "syntax.hpp"

#include <baker/parse.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

using namespace baker;
namespace syntax = baker::experiments::syntax;

namespace {

// As the core harness's session: no includes, and limits a fuzzer cannot outrun.
LoadSession make_session() {
    LoadSession s;
    s.set_reader([](std::string_view, TextSink) { return false; });
    s.limits.max_depth = 64;
    s.limits.max_nodes = 1u << 16;
    return s;
}

// Walks the tree as if fully expanded, stopping when `budget` runs out.
bool fits(const Node& n, uint64_t& budget) {
    if (budget == 0) return false;
    --budget;
    if (const Array* a = n.array())
        for (const Node& c : *a)
            if (!fits(c, budget)) return false;
    return true;
}

bool holds(std::string_view data, const syntax::ReadOptions& options) {
    LoadSession s = make_session();
    auto first = syntax::read(data, "fuzz.dta", s, options);
    if (!first) {
        const Error& e = first.error();
        const auto lines = 1 + std::count(data.begin(), data.end(), '\n');
        return !e.message().empty() && e.file() == Symbol("fuzz.dta") && e.line() >= 1 &&
               e.line() <= lines;
    }
    // A tree that macro sharing made larger than the reader's node limit is skipped: its
    // print cannot be read back under the limits the text was read with.
    uint64_t budget = s.limits.max_nodes;
    for (const Node& n : **first)
        if (!fits(n, budget)) return true;
    // A first form (origin "...") kept as data, a second origin, prints as the declaration
    // the reader consumes (D91): main's printer cannot spell it either, so it is not this
    // form's to fix.
    if (const Array* a = (*first)->size() > 0 ? (**first)[0].try_array() : nullptr;
        a && a->tag() == Symbol("origin") && a->size() > 1 && (*a)[1].type() == Type::String)
        return true;
    const std::string printed = syntax::print(**first);
    LoadSession again = make_session();
    auto second = syntax::read(printed, "printed.dta", again, options);
    return second && structurally_equal(**first, **second);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    if (!holds(text, {}) || !holds(text, {.trailing_children = false})) std::abort();
    return 0;
}
