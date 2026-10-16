#include "base/sim.hpp"

#include <baker/parse.hpp>

#include <doctest.h>

#include "test_util.hpp"

namespace {

// A game with no window: a counter that steps, and the script words to drive and read it.
struct Counter final : base::Game {
    double total = 0, rate = 1;
    uint64_t seed = 0;
    void bind(baker::Context& ctx) override {
        (void)ctx.func("counter.total", [this] { return total; });
        (void)ctx.func("counter.rate", [this](double r) { rate = r; });
    }
    void reset(uint64_t s) override {
        total = 0;
        rate = 1;
        seed = s;
    }
    void step(double dt) override { total += rate * dt; }
    void draw(base::Renderer&, float) override {}
};

struct Fixture {
    test::ScratchDir dir{"sim"};
    std::vector<base::Objective> objectives;
    std::vector<baker::Ref<baker::Array>> trees;
    std::vector<base::Scenario> scenarios;
    std::string error;

    bool load(const std::string& metrics, const std::string& scenario) {
        std::filesystem::create_directories(dir.path / "metrics");
        return test::write(dir.path / "metrics" / "m.dta", metrics) &&
               test::write(dir.path / "s.dta", scenario) &&
               base::load_objectives(dir.path / "metrics", objectives, trees, error) &&
               base::load_scenarios(dir.path / "s.dta", scenarios, error);
    }
};

const char* kMetrics = R"(
(objective counter.reached (desc "Ten seconds at rate 1 reach 10")
   (measure {counter.total}) (pass {>= $value 9.99}))
(objective counter.small (measure {counter.total}) (pass {< $value 5})))";

}  // namespace

TEST_CASE("sim: steps a scenario's seconds and judges it") {
    Fixture f;
    REQUIRE_MESSAGE(f.load(kMetrics, "(scenario ten (seconds 10) (objectives counter.reached))"),
                    f.error);
    REQUIRE(f.objectives.size() == 2);
    REQUIRE(f.scenarios.size() == 1);

    Counter game;
    baker::Context ctx;
    game.bind(ctx);
    const base::RunResult r = base::simulate(game, ctx, f.scenarios[0], f.objectives, 7, 60);
    CHECK(r.steps == 600);
    CHECK(game.seed == 7);
    REQUIRE(r.objectives.size() == 1);  // the scenario names only one
    CHECK(r.objectives[0].value == doctest::Approx(10.0));
    CHECK(r.objectives[0].pass);
    CHECK(r.passed());
}

TEST_CASE("sim: start and at commands run at their times; a failing objective fails the run") {
    Fixture f;
    REQUIRE_MESSAGE(f.load(kMetrics, R"((scenario timed (seconds 4)
        (start {counter.rate 2})
        (at 2 {counter.rate 0}))   ; 2 s at rate 2, then still
    )"),
                    f.error);
    Counter game;
    baker::Context ctx;
    game.bind(ctx);
    const base::RunResult r = base::simulate(game, ctx, f.scenarios[0], f.objectives, 1, 60);
    REQUIRE(r.objectives.size() == 2);  // no (objectives ...): all of them
    CHECK(r.objectives[0].value == doctest::Approx(4.0));
    CHECK_FALSE(r.objectives[0].pass);  // counter.reached wanted 10
    CHECK(r.objectives[1].pass);        // counter.small
    CHECK_FALSE(r.passed());
}

TEST_CASE("sim: the same scenario and seed give the same result at any step rate's count") {
    Fixture f;
    REQUIRE(f.load(kMetrics, "(scenario ten (seconds 10))"));
    Counter game;
    baker::Context ctx;
    game.bind(ctx);
    const auto a = base::simulate(game, ctx, f.scenarios[0], f.objectives, 3, 60);
    const auto b = base::simulate(game, ctx, f.scenarios[0], f.objectives, 3, 60);
    CHECK(base::to_dta(a) == base::to_dta(b));  // reset between runs: nothing leaks
}

TEST_CASE("sim: errors are reported, not crashed on") {
    Fixture f;
    REQUIRE(f.load(kMetrics, "(scenario bad (seconds 1) (start {no.such}) (objectives nope))"));
    Counter game;
    baker::Context ctx;
    game.bind(ctx);
    const auto r = base::simulate(game, ctx, f.scenarios[0], f.objectives, 1, 60);
    CHECK_FALSE(r.passed());
    REQUIRE(r.errors.size() == 2);  // the unknown function, and the unknown objective
    CHECK(r.errors[1].find("nope") != std::string::npos);

    Fixture g;
    CHECK_FALSE(g.load("(objective broken (measure {counter.total}))", "(scenario s (seconds 1))"));
    CHECK(g.error.find("pass") != std::string::npos);
    Fixture h;
    CHECK_FALSE(h.load(kMetrics, "(scenario s)"));
    CHECK(h.error.find("seconds") != std::string::npos);
}

TEST_CASE("sim: results are data that reads back") {
    Fixture f;
    REQUIRE(f.load(kMetrics, "(scenario bad (seconds 1) (start {no.such \"a \\q quote\"}))"));
    Counter game;
    baker::Context ctx;
    game.bind(ctx);
    std::vector<base::RunResult> runs{
        base::simulate(game, ctx, f.scenarios[0], f.objectives, 1, 60),
        base::simulate(game, ctx, f.scenarios[0], f.objectives, 2, 60)};
    REQUIRE(
        test::write(f.dir.path / "out.dta", base::to_dta(runs[0]) + base::summary_dta(runs, {})));
    auto back = baker::read_file((f.dir.path / "out.dta").string());
    REQUIRE_MESSAGE(back, base::to_dta(runs[0]));
    CHECK((*back)->get<int64_t>("summary.runs").value_or(0) == 2);
    CHECK((*back)->get<int64_t>("run.seed").value_or(0) == 1);
}

TEST_CASE("sim: an objective (over median) is judged by its median over a scenario's seeds") {
    Fixture f;
    // Each seed's total is the seed: 1 to 5, so the median is 3.
    REQUIRE_MESSAGE(f.load(R"(
        (objective counter.typical (over median) (measure {counter.total}) (pass {<= $value 3.5}))
        (objective counter.strict (over median) (measure {counter.total}) (pass {<= $value 2.5})))",
                           "(scenario seeded (seconds 1) (start {counter.rate $seed}))"),
                    f.error);
    REQUIRE(f.objectives.size() == 2);
    CHECK(f.objectives[0].over_median);
    Counter game;
    baker::Context ctx;
    game.bind(ctx);
    std::vector<base::RunResult> runs;
    for (uint64_t seed = 1; seed <= 5; ++seed)
        runs.push_back(base::simulate(game, ctx, f.scenarios[0], f.objectives, seed, 60));
    // A seed whose value is out of band does not fail its run.
    CHECK_FALSE(runs[4].objectives[0].pass);
    CHECK(runs[4].passed());
    const auto medians = base::judge_medians(ctx, f.objectives, runs);
    REQUIRE(medians.size() == 2);
    CHECK(medians[0].objective == "counter.strict");
    CHECK(medians[0].value == doctest::Approx(3.0));
    CHECK(medians[0].seeds == 5);
    CHECK_FALSE(medians[0].pass);
    CHECK(medians[1].pass);  // counter.typical
    CHECK_FALSE(base::all_passed(runs, medians));
    CHECK(base::all_passed(runs, {medians[1]}));

    // The verdicts are data that reads back, and compare judges by them.
    std::filesystem::create_directories(f.dir.path / "out");
    for (const base::RunResult& r : runs)
        REQUIRE(test::write(f.dir.path / "out" / ("run-seeded-" + std::to_string(r.seed) + ".dta"),
                            base::to_dta(r)));
    REQUIRE(test::write(f.dir.path / "out" / "medians.dta", base::medians_dta(medians)));
    std::vector<base::RunResult> back;
    std::vector<base::MedianResult> back_medians;
    REQUIRE_MESSAGE(base::read_runs(f.dir.path / "out", back, back_medians, f.error), f.error);
    REQUIRE(back_medians.size() == 2);
    CHECK(back_medians[1].pass);
    CHECK(back[0].objectives[0].over_median);

    bool broke = false;
    // The strict objective's median fails on both sides: the same, whatever the seeds did.
    std::string json = base::compare_json(back, back_medians, runs, medians, broke);
    CHECK_FALSE(broke);
    // The typical objective's median passed, and now fails: broke.
    std::vector<base::MedianResult> worse = medians;
    worse[1].pass = false;
    json = base::compare_json(runs, medians, runs, worse, broke);
    CHECK(broke);
    CHECK(json.find("\"over\": \"median\"") != std::string::npos);
}
