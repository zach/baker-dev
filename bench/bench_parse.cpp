// Parse throughput on generated config-style text. Target (spec §7): >= 40 MB/s.
#include <moira/parse.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

int main() {
    std::string text;
    int id = 0;
    while (text.size() < 4 * 1024 * 1024) {
        const std::string n = std::to_string(id++);
        text += "; entry " + n + "\n(entry_" + n + "\n   (name \"Entry number " + n +
                "\")\n"
                "   (enabled TRUE)\n   (position " +
                n +
                ".5 -12.25 0.0)\n"
                "   (color 0.2 0.4 0.8 1.0)\n   (tags fast loud blue)\n"
                "   (limits (min 0) (max 100) (step 0.5))\n"
                "   (on_hit {play_sfx \"hit.wav\"} {game add_score 10}))\n";
    }
    double best = 1e9;
    for (int rep = 0; rep < 10; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        auto r = moira::try_read_string(text, "bench.dta");
        const auto t1 = std::chrono::steady_clock::now();
        if (!r) return 1;
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    }
    std::printf("parse: %.1f MB in %.2f ms best of 10 -> %.1f MB/s\n",
                static_cast<double>(text.size()) / 1e6, best * 1e3,
                static_cast<double>(text.size()) / 1e6 / best);
    return 0;
}
