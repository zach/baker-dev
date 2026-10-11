// Native call cost. Target (spec §7): a native function call through the binding layer
// with 2 scalar args <= 40 ns, with zero allocations.
//
// Each row evaluates a parsed command node with Context::try_eval, as a game evaluating
// its data would. The last rows compare the per-Context table that resolves a function
// name (docs/decisions.md D44) with a hash map, the lookup D5's in-node cache would save.
#include <moira/parse.hpp>
#include <moira/script.hpp>

#include "alloc_counter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

volatile double g_sink;

template <class T>
const T& opaque(const T& v) {
    const T* volatile p = &v;
    return *p;
}

template <class F>
double ns_per_call(F&& f) {
    constexpr int kCalls = 2'000'000;
    double best = 1e30;
    for (int rep = 0; rep < 7; ++rep) {
        double sum = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kCalls; ++i) sum += f(i);
        const auto t1 = std::chrono::steady_clock::now();
        g_sink = sum;
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / kCalls);
    }
    return best;
}

}  // namespace

int main() {
    moira::Context ctx;
    if (!ctx.try_func("add", [](int64_t a, int64_t b) { return a + b; })) return 1;
    ctx.var("x") = 4;
    if (!ctx.try_eval("{func add1 ($a) {+ $a 1}}")) return 1;
    auto tree = moira::try_read_string(
        "{add 1 2} {+ 1 2} {+ 1.5 2} {add $x 1} $x {if 1 2 3} {add1 1} {add {add 1 2} 3}",
        "bench.dta");
    if (!tree) return 1;
    const char* names[] = {
        "{add 1 2}: native, 2 int args",
        "{+ 1 2}: overload set (int int)",
        "{+ 1.5 2}: overload set, promoted",
        "{add $x 1}: a variable argument",
        "$x: a variable",
        "{if 1 2 3}: lazy branches",
        "{add1 1}: script function",
        "{add {add 1 2} 3}: nested",
    };

    std::printf("evaluating a command node (best of 7, 2M calls each)\n");
    for (size_t k = 0; k < (*tree)->size(); ++k) {
        const moira::Node& node = (**tree)[k];
        auto call = [&](int) {
            auto r = ctx.try_eval(opaque(node));
            return r ? static_cast<double>(r->try_float().value_or(0)) : -1.0;
        };
        for (int i = 0; i < 100; ++i) call(i);
        moira::test::AllocationScope scope;
        for (int i = 0; i < 1000; ++i) call(i);
        const size_t allocs = scope.count();
        const double ns = ns_per_call(call);
        if (k == 0)
            std::printf("%-38s %7.2f ns  %zu allocations / 1000 calls   (target <= 40 ns, 0: %s)\n",
                        names[k], ns, allocs, ns <= 40 && allocs == 0 ? "ok" : "MISSED");
        else
            std::printf("%-38s %7.2f ns  %zu allocations / 1000 calls\n", names[k], ns, allocs);
    }

    // Resolving a name: the Context's table, indexed by Symbol::index(), against a hash map.
    std::vector<moira::Symbol> keys;
    std::unordered_map<moira::Symbol, int> map;
    std::vector<int> table;
    for (int i = 0; i < 200; ++i) {
        keys.emplace_back("fn" + std::to_string(i));
        map[keys.back()] = i;
        if (keys.back().index() >= table.size()) table.resize(keys.back().index() + 1);
        table[keys.back().index()] = i;
    }
    std::printf("\nresolving a function name among 200\n");
    std::printf("%-38s %7.2f ns\n", "table by Symbol::index()", ns_per_call([&](int i) {
                    const uint32_t ix = opaque(keys[static_cast<size_t>(i % 200)]).index();
                    return ix < table.size() ? double(table[ix]) : -1.0;
                }));
    std::printf("%-38s %7.2f ns\n", "std::unordered_map<Symbol>", ns_per_call([&](int i) {
                    return double(map.find(opaque(keys[static_cast<size_t>(i % 200)]))->second);
                }));
    return 0;
}
