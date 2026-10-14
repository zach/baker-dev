#include "puzzle.hpp"

#include <moira/parse.hpp>

#include <doctest.h>

#include <string>

namespace {

const char* kData = R"(
(kinds
   (ball   (shape sphere) (size 0.4) (mass 0.5) (friction 0.4) (bounce 0.2))
   (plank  (shape box) (size 2.5 0.15) (friction 0.4))
   (ramp   (shape box) (size 4 0.2) (friction 0.3))
   (bucket (shape bucket) (size 1.6 0.8)))
(levels
   (drop
      (desc "a plank to the bucket")
      (bounds -7 0 7 8)
      (pieces
         (ball (kind ball) (pos -6 6.5))
         (chute (kind ramp) (pos -4.5 5) (angle -25) (fixed 1))
         (cup (kind bucket) (pos 4.5 0.4)))
      (tray (plank 1))
      (intended (p1 (kind plank) (pos 0.5 2) (angle 0))))
   (gift
      (desc "the ball starts over the bucket: no piece needed")
      (bounds -7 0 7 8)
      (pieces (ball (kind ball) (pos 4.5 3)) (cup (kind bucket) (pos 4.5 0.4)))
      (tray (plank 1))
      (intended (p1 (kind plank) (pos 0 6)))))
)";

contraption::Catalog catalog() {
    auto tree = moira::read_string(kData);
    REQUIRE(tree);
    contraption::Catalog c;
    std::string error;
    REQUIRE_MESSAGE(contraption::load(*tree, c, error), error);
    return c;
}

// The goal the data would write as {puzzle.in ball cup}, in C++.
bool ball_in_cup(contraption::Run& run) {
    return run.inside(run.find("ball"), run.find("cup"));
}

}  // namespace

TEST_CASE("contraption: levels and kinds read from data") {
    const contraption::Catalog c = catalog();
    REQUIRE(c.levels.size() == 2);
    const contraption::Level& l = c.levels[0];
    CHECK(l.pieces.size() == 3);
    CHECK(l.pieces[1].fixed);
    REQUIRE(l.tray.size() == 1);
    CHECK(l.tray[0].second == 1);
    CHECK(c.kinds[size_t(l.intended[0].kind)].name == "plank");
    CHECK(c.kinds[size_t(c.kind("bucket"))].shape == contraption::Shape::Bucket);

    contraption::Catalog bad;
    std::string error;
    auto tree = moira::read_string("(kinds (a (shape box))) (levels (l (pieces (p (kind nope)))))");
    REQUIRE(tree);
    CHECK_FALSE(contraption::load(*tree, bad, error));
    CHECK(error.find("kind") != std::string::npos);
}

TEST_CASE("contraption: the intended solution solves; nothing placed does not") {
    const contraption::Catalog c = catalog();
    const contraption::Level& l = c.levels[0];
    contraption::Rules rules;
    const auto solved = contraption::attempt(c, l, l.intended, rules, ball_in_cup);
    CHECK(solved.solved);
    CHECK(solved.seconds > 1);
    CHECK_FALSE(contraption::attempt(c, l, {}, rules, ball_in_cup).solved);
    CHECK(contraption::robustness(c, l, rules, ball_in_cup, 10, 0.005f, 2, 1) >= 0.8);
}

TEST_CASE("contraption: the cheese search finds a level that needs fewer pieces") {
    const contraption::Catalog c = catalog();
    contraption::Rules rules;
    const auto none = contraption::cheese(c, c.levels[0], rules, ball_in_cup, 20, 1);
    CHECK(none.fewest == -1);
    const auto gift = contraption::cheese(c, c.levels[1], rules, ball_in_cup, 20, 1);
    CHECK(gift.fewest == 0);  // the ball falls in on its own
    CHECK(gift.tried == 1);
}

TEST_CASE("contraption: a run replays bit for bit, on every build") {
    const contraption::Catalog c = catalog();
    const contraption::Level& l = c.levels[0];
    auto replay = [&] {
        contraption::Run run(c, l, l.intended);
        for (int i = 0; i < 300; ++i) run.step(1.0f / 60.0f);
        return run.physics().hash();
    };
    const uint64_t h = replay();
    CHECK(h == replay());
    // Computed on Linux with GCC; the MSVC job checks it on Windows (game-stacks.md §4.1:
    // the runner and the game agree).
    MESSAGE("drop replay hash: ", h);
    CHECK(h == 145017875462435393ull);
}
