// The stacks' random numbers (D115): xorshift64*, seeded per stream, so a run is a function
// of its seed on every compiler and platform (the C++ library's distributions are not).
//
//   base::Rng rng(seed, 0x5851F42D4C957F2Dull);  // a stream of its own per use
//   float x = rng.uniform(-1, 1);
//   size_t pick = rng.next() % options.size();
#pragma once

#include <cstdint>

namespace base {

struct Rng {
    uint64_t state = 1;

    Rng() = default;
    // Seed `seed` of stream `stream`: different streams of one seed draw independently.
    Rng(uint64_t seed, uint64_t stream) noexcept : state(seed * 0x9E3779B97F4A7C15ull + stream) {
        if (state == 0) state = 1;
    }
    // A generator already at `state` (never 0).
    [[nodiscard]] static Rng at(uint64_t state) noexcept {
        Rng r;
        r.state = state ? state : 1;
        return r;
    }

    uint64_t next() noexcept {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }
    // In [0, 1), from the top 53 bits.
    double unit() noexcept {
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }
    // In [lo, hi).
    float uniform(float lo, float hi) noexcept {
        return lo + static_cast<float>(unit()) * (hi - lo);
    }
};

}  // namespace base
