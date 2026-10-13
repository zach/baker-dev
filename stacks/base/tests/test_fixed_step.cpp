#include "base/fixed_step.hpp"

#include <doctest.h>

TEST_CASE("fixed step: whole steps, the rest carried") {
    base::FixedStep clock{.step_ns = 250};
    CHECK(clock.advance(600) == 2);
    CHECK(clock.alpha() == doctest::Approx(0.4));
    CHECK(clock.advance(150) == 1);  // 100 carried + 150
    CHECK(clock.alpha() == 0.0f);
    CHECK(clock.advance(0) == 0);
}

TEST_CASE("fixed step: a stall drops time instead of spiralling") {
    base::FixedStep clock{.step_ns = 10, .max_steps = 4};
    CHECK(clock.advance(1000) == 4);
    CHECK(clock.carried_ns == 0);
    CHECK(clock.advance(15) == 1);
}

TEST_CASE("fixed step: the same elapsed time gives the same steps at any frame rate") {
    base::FixedStep a{.step_ns = base::FixedStep::hz(50)}, b{.step_ns = base::FixedStep::hz(50)};
    int sa = 0, sb = 0;
    for (int i = 0; i < 100; ++i) sa += a.advance(10'000'000);  // 100 Hz frames
    for (int i = 0; i < 25; ++i) sb += b.advance(40'000'000);   // 25 Hz frames
    CHECK(sa == 50);
    CHECK(sb == 50);
    CHECK(base::FixedStep::hz(60) == 16'666'667);
}
