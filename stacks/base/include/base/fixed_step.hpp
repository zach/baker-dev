// A fixed simulation step, independent of the frame rate: the simulation advances in
// whole steps, so a run replays the same at any frame rate, and the renderer interpolates
// between the last two states by alpha(). Time is counted in integer nanoseconds, so the
// same elapsed time always gives the same steps: seconds summed as doubles drift.
//
//   base::FixedStep clock{.step_ns = 16'666'667};
//   for (int n = clock.advance(frame_ns); n > 0; --n) game.step(clock.step_seconds());
//   game.draw(clock.alpha());
#pragma once

#include <cstdint>

namespace base {

struct FixedStep {
    uint64_t step_ns = 16'666'667;  // 60 Hz
    int max_steps = 8;        // per frame; time beyond them is dropped, so a stall cannot spiral
    uint64_t carried_ns = 0;  // time not yet stepped, under one step after advance()

    [[nodiscard]] static constexpr uint64_t hz(double rate) noexcept {
        return static_cast<uint64_t>(1e9 / rate + 0.5);
    }
    [[nodiscard]] double step_seconds() const noexcept {
        return static_cast<double>(step_ns) / 1e9;
    }

    // Adds a frame's time and returns how many steps to run for it.
    [[nodiscard]] int advance(uint64_t ns) noexcept {
        carried_ns += ns;
        int n = 0;
        while (carried_ns >= step_ns && n < max_steps) {
            carried_ns -= step_ns;
            ++n;
        }
        if (carried_ns >= step_ns) carried_ns = 0;
        return n;
    }
    // How far past the last step the frame is, from 0 up to 1.
    [[nodiscard]] float alpha() const noexcept {
        return static_cast<float>(static_cast<double>(carried_ns) / static_cast<double>(step_ns));
    }
};

}  // namespace base
