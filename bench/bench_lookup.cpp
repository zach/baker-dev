// Lookup cost. Target (spec §7): find_float on a 20-child array <= 25 ns.
//
// Keys are interned Symbols, as a hot loop should hold them; the string-literal row
// shows what interning on every call costs instead. "avg" cycles through all 20 keys,
// "last" always asks for the 20th (the linear scan's worst case).
#include <moira/array.hpp>
#include <moira/parse.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

volatile double g_sink;

// Read through a volatile pointer, so the compiler cannot hoist a lookup with a constant
// key out of the timing loop.
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

void row(const char* name, double ns, double target = 0) {
    if (target > 0)
        std::printf("%-44s %7.2f ns   (target <= %.0f ns: %s)\n", name, ns, target,
                    ns <= target ? "ok" : "MISSED");
    else
        std::printf("%-44s %7.2f ns\n", name, ns);
}

}  // namespace

int main() {
    std::string text = "(obj\n";
    for (int i = 0; i < 20; ++i)
        text += "  (key" + std::to_string(i) + " " + std::to_string(i) + ".5)\n";
    text += ")\n(cfg (renderer (clear_color 0.1 0.2 0.3) (shadows (resolution 2048))))\n";
    auto root = moira::try_read_string(text, "bench.dta");
    if (!root) return 1;
    const moira::Array& obj = *(*root)->try_find("obj");

    std::vector<moira::Symbol> keys;
    for (int i = 0; i < 20; ++i) keys.emplace_back("key" + std::to_string(i));
    const moira::Symbol last = keys.back();
    const moira::Symbol absent("absent");

    auto sorted_ref = moira::Array::make_copy(obj.nodes());
    sorted_ref->sort_for_lookup();
    const moira::Array& sorted = *sorted_ref;

    const moira::Path path("cfg.renderer.shadows.resolution");
    const moira::Path path2("cfg.renderer.clear_color.1");

    std::printf("lookup on a 20-child array (best of 7, 2M calls each)\n");
    row("try_find (array), avg", ns_per_call([&](int i) {
            return static_cast<double>(
                obj.try_find(opaque(keys[static_cast<size_t>(i % 20)]))->size());
        }));
#if MOIRA_EXCEPTIONS
    row("find_float, avg", ns_per_call([&](int i) {
            return obj.find_float(opaque(keys[static_cast<size_t>(i % 20)]));
        }),
        25);
    row("find_float, last", ns_per_call([&](int) { return obj.find_float(opaque(last)); }), 25);
    row("find_float, sorted, avg", ns_per_call([&](int i) {
            return sorted.find_float(opaque(keys[static_cast<size_t>(i % 20)]));
        }));
    row("find_float, sorted, last",
        ns_per_call([&](int) { return sorted.find_float(opaque(last)); }));
    row("find<float>, avg", ns_per_call([&](int i) {
            return static_cast<double>(obj.find<float>(opaque(keys[static_cast<size_t>(i % 20)])));
        }));
    row("find_float(\"key19\") (interns each call)",
        ns_per_call([&](int) { return obj.find_float("key19"); }));
    row("get<int>(Path), 4 segments",
        ns_per_call([&](int) { return static_cast<double>((*root)->get<int>(opaque(path))); }));
    row("get<float>(Path), 4 segments with index",
        ns_per_call([&](int) { return static_cast<double>((*root)->get<float>(opaque(path2))); }));
    row("get<int>(\"...\") (parses each call)", ns_per_call([&](int) {
            return static_cast<double>((*root)->get<int>("cfg.renderer.shadows.resolution"));
        }));
#endif
    // Where sorted mode starts to pay: a linear scan grows with the array, binary search
    // barely does.
    for (const int n : {50, 200, 1000}) {
        std::vector<moira::Node> big;
        std::vector<moira::Symbol> big_keys;
        for (int i = 0; i < n; ++i) {
            big_keys.emplace_back("big" + std::to_string(i));
            big.emplace_back(moira::array(big_keys.back(), i));
        }
        auto linear = moira::Array::make_from(big);
        auto sorted_big = moira::Array::make_copy(linear->nodes());
        sorted_big->sort_for_lookup();
        const auto un = static_cast<unsigned>(n);
        const double l = ns_per_call([&](int i) {
            return static_cast<double>(
                linear->try_find(opaque(big_keys[static_cast<unsigned>(i) % un]))->size());
        });
        const double s = ns_per_call([&](int i) {
            return static_cast<double>(
                sorted_big->try_find(opaque(big_keys[static_cast<unsigned>(i) % un]))->size());
        });
        std::printf("try_find, %4d children, avg: linear %7.2f ns, sorted %6.2f ns\n", n, l, s);
    }
    row("try_find_float, avg", ns_per_call([&](int i) {
            return *obj.try_find_float(opaque(keys[static_cast<size_t>(i % 20)]));
        }));
    row("try_find_float, last",
        ns_per_call([&](int) { return *obj.try_find_float(opaque(last)); }));
    row("try_find_float(fallback), absent",
        ns_per_call([&](int) { return *obj.try_find_float(opaque(absent), 1.0); }));
    return 0;
}
