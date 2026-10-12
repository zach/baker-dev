// Tasks (M7 plan, D58, D67). Targets: a tick with 10,000 tasks asleep and none due under
// 100 ns; resuming a C++ task <= 20 ns; spawning allocates nothing once the pool is warm.
#include <moira/script.hpp>
#include <moira/task.hpp>

#include "alloc_counter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {

template <class F>
double best_ns(int reps, int per_rep, F&& f) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / per_rep);
    }
    return best;
}

moira::Task sleeper(moira::Scheduler&) {
    co_await moira::delay(1e9);
}
moira::Task stepper(moira::Scheduler&, long* n) {
    for (;;) {
        ++*n;
        co_await moira::next_tick();
    }
}
moira::Task once(moira::Scheduler&, long* n) {
    ++*n;
    co_return;
}

}  // namespace

int main() {
    moira::Context ctx;
    {
        moira::Scheduler s(ctx);
        for (int i = 0; i < 10'000; ++i) (void)s.spawn(sleeper(s));
        s.tick(0);  // start them: each parks on its delay
        const double ns = best_ns(7, 10'000, [&] {
            for (int i = 0; i < 10'000; ++i) s.tick(0.001);
        });
        std::printf("%-44s %8.2f ns   (target < 100 ns)\n", "tick, 10,000 asleep, none due", ns);
    }
    {
        moira::Scheduler s(ctx);
        long n = 0;
        for (int i = 0; i < 1'000; ++i) (void)s.spawn(stepper(s, &n));
        s.tick(0);
        const double ns = best_ns(7, 1'000 * 100, [&] {
            for (int i = 0; i < 100; ++i) s.tick(1);
        });
        std::printf("%-44s %8.2f ns   (target <= 20 ns)\n",
                    "resume, 1,000 tasks on next_tick, per task", ns);
        std::printf("%-44s %8u bytes\n", "that task's frame", s.stats().largest_frame);
    }
    {
        moira::Scheduler s(ctx);
        long n = 0;
        for (int i = 0; i < 1'000; ++i) (void)s.spawn(once(s, &n));
        s.tick(0);  // warms the pool and the tables
        moira::test::AllocationScope scope;
        const double ns = best_ns(7, 1'000, [&] {
            for (int i = 0; i < 1'000; ++i) (void)s.spawn(once(s, &n));
            s.tick(0);
        });
        std::printf("%-44s %8.2f ns   %zu allocations\n", "spawn + run to completion, per task", ns,
                    scope.count());
    }
    return 0;
}
