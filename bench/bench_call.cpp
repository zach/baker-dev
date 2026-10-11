// Native call cost. Target (spec §7): a native function call through the binding layer
// with 2 scalar args <= 40 ns, with zero allocations.
//
// Each row evaluates a parsed command node with Context::eval, as a game evaluating
// its data would. The last rows compare the per-Context table that resolves a function
// name (docs/decisions.md D44) with a hash map, the lookup D5's in-node cache would save.
#include <moira/object.hpp>
#include <moira/parse.hpp>
#include <moira/script.hpp>
#include <moira/std_interop.hpp>

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

struct HitMsg {
    static constexpr auto name = "hit";
    int points;
};

struct Person : moira::Object<Person> {
    float height = 6.0f;
    int hp = 1'000'000;
    float on_grow(float amount) { return height += amount; }
    int on_hit(const HitMsg& m) { return hp -= m.points; }
    static void bind(moira::Binder<Person>& b) {
        b.method("grow", &Person::on_grow);
        b.on<HitMsg>(&Person::on_hit);
    }
};

int main() {
    moira::Context ctx;
    Person bob;
    if (!ctx.add(bob, "bob")) return 1;
    if (!ctx.func("add", [](int64_t a, int64_t b) { return a + b; })) return 1;
    ctx.var("x") = 4;
    // Array-valued variables: reading one copies the node, which counts the array's owner.
    auto loaded = moira::read_string("(1 2 3)", "list.dta");
    if (!loaded) return 1;
    ctx.var("loaded") = (**loaded)[0];
    ctx.var("solo") = moira::Node(moira::Array::make(3));
    if (!ctx.eval("{func add1 ($a) {+ $a 1}}")) return 1;
    auto tree = moira::read_string(
        "{add 1 2} {+ 1 2} {+ 1.5 2} {add $x 1} $x {if 1 2 3} {add1 1} {add {add 1 2} 3} "
        "{bob grow 0} $loaded $solo",
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
        "{bob grow 0}: native method",
        "$loaded: an array from a load",
        "$solo: an array built by the host",
    };

    std::printf("evaluating a command node (best of 7, 2M calls each)\n");
    for (size_t k = 0; k < (*tree)->size(); ++k) {
        const moira::Node& node = (**tree)[k];
        auto call = [&](int) {
            auto r = ctx.eval(opaque(node));
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

    // Call statistics and tracing (D53): counting is always on outside shipping builds;
    // timing and tracing are switched on here.
    std::printf("\n{add 1 2} with the development aids switched on\n");
    {
        const moira::Node& add = (**tree)[0];
        auto call = [&](int) {
            auto r = ctx.eval(opaque(add));
            return r ? static_cast<double>(r->try_float().value_or(0)) : -1.0;
        };
        ctx.set_profiling(true);
        std::printf("%-38s %7.2f ns\n", "profiling (two steady_clock reads)", ns_per_call(call));
        ctx.set_profiling(false);
        ctx.set_trace_sink([](void*, const moira::TraceEvent&) {}, nullptr);
        ctx.trace(true);
        std::printf("%-38s %7.2f ns\n", "tracing to an empty sink", ns_per_call(call));
        ctx.trace(false);
    }

    // Typed messages (spec §7: C++ to C++ dispatch <= 15 ns, zero allocations).
    std::printf("\nsending a message from C++\n");
    {
        auto row = [](const char* name, auto&& call, const char* target) {
            for (int i = 0; i < 100; ++i) call(i);
            moira::test::AllocationScope scope;
            for (int i = 0; i < 1000; ++i) call(i);
            const size_t allocs = scope.count();
            const double ns = ns_per_call(call);
            std::printf("%-38s %7.2f ns  %zu allocations / 1000 calls%s\n", name, ns, allocs,
                        target);
        };
        Person loose;
        Person* volatile to_bob = &bob;  // opaque, and not const
        Person* volatile to_loose = &loose;
        row(
            "bob.handle(HitMsg{1}), in a Context",
            [&](int i) {
                auto r = to_bob->handle(HitMsg{i & 1});
                return r ? static_cast<double>(r->try_int().value_or(0)) : -1.0;
            },
            "   (target <= 15 ns)");
        row(
            "handle(HitMsg{1}), in no Context",
            [&](int i) {
                auto r = to_loose->handle(HitMsg{i & 1});
                return r ? static_cast<double>(r->try_int().value_or(0)) : -1.0;
            },
            "");
        std::vector<Person> crowd(64);
        std::vector<moira::ObjectBase*> ptrs;
        for (Person& p : crowd) ptrs.push_back(&p);
        row(
            "handle_all, one call for 64 objects",
            [&](int i) {
                auto r =
                    moira::handle_all(std::span<moira::ObjectBase* const>(ptrs), HitMsg{i & 1});
                return r ? static_cast<double>(*r) : -1.0;
            },
            "   (the time is for 64)");
        row(
            "ctx.call(bob, \"grow\", 1.5f), for comparison",
            [&](int) {
                auto r = ctx.call(opaque(bob), "grow", 1.5f);
                return r ? r->try_float().value_or(0) : -1.0;
            },
            "");
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
