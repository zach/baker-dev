#include "parse_harness.hpp"

#include <baker/cache.hpp>
#include <baker/parse.hpp>
#include <baker/source_map.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace baker::fuzz {
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

// Edit mode: every recorded span lies within the text, and a literal scalar token read on
// its own is the node it produced. Visits up to `budget` arrays.
bool spans_hold(const Node& n, const SourceMap& map, std::string_view text, uint64_t& budget) {
    const Array* a = n.array();
    if (!a || budget == 0) return true;
    --budget;
    if (const auto spans = map.spans(*a)) {
        for (size_t i = 0; i < a->size(); ++i) {
            const SourceSpan& sp = (*spans)[i];
            if (sp.file.empty()) continue;
            if (sp.begin > sp.end || sp.end > text.size()) return false;
            if (!sp.literal() || (*a)[i].array()) continue;
            // A leading space, so a token starting with BOM bytes is not read as a BOM.
            const std::string token = " " + std::string(text.substr(sp.begin, sp.end - sp.begin));
            // The marker reads only as a key's one value (D143), never alone.
            if (token == " #remove") continue;
            auto alone = read_string(token, "token.dta");
            if (!alone || (*alone)->size() != 1 || !structurally_equal((**alone)[0], (*a)[i]))
                return false;
        }
    }
    for (const Node& c : *a)
        if (!spans_hold(c, map, text, budget)) return false;
    return true;
}

LoadSession make_session() {
    LoadSession s;
    s.set_reader([](std::string_view, TextSink) { return false; });
    s.limits.max_depth = 64;
    s.limits.max_nodes = 1u << 16;
    return s;
}

}  // namespace

bool check_parse(std::string_view data) {
    LoadSession s = make_session();
    auto first = read_string(data, "fuzz.dta", s);
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

    // The same text read in edit mode gives the same tree, with spans that hold.
    LoadSession edit = make_session();
    SourceMap map;
    edit.source_map = &map;
    auto edited = read_string(data, "fuzz.dta", edit);
    if (!edited || !structurally_equal(**first, **edited)) return false;
    uint64_t span_budget = 2000;
    if (!spans_hold(Node(*edited), map, data, span_budget)) return false;

    // The binary cache gives the same tree back.
    std::string bytes;
    if (!write_cache(**first, string_out(bytes))) return false;
    auto cached = read_cache_bytes(bytes);
    if (!cached || !structurally_equal(**first, **cached)) return false;

    // A first form (origin "...") held as data, a second origin, would print as the
    // declaration the reader consumes (D91); the printer writes it as a row instead (D249),
    // which a top level that repeats a tag cannot hold (D31): that tree text cannot spell
    // (D13).
    if (const Array* a = (*first)->size() > 0 ? (**first)[0].try_array() : nullptr;
        a && a->tag() == Symbol("origin") && a->size() > 1 && (*a)[1].type() == Type::String)
        for (uint32_t i = 1; i < (*first)->size(); ++i)
            for (uint32_t j = 0; j < i; ++j) {
                const Array* x = (**first)[i].try_array();
                const Array* y = (**first)[j].try_array();
                if (x && y && !x->tag().empty() && x->tag() == y->tag()) return true;
            }

    const std::string printed = to_dta(**first);
    LoadSession s2 = make_session();
    auto second = read_string(printed, "printed.dta", s2);
    return second && structurally_equal(**first, **second);
}

}  // namespace baker::fuzz
