// The experiment's tests: the corpus round trip, one case per form, and error cases. An
// experiment keeps the first and replaces the rest.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/std_interop.hpp>

#include <doctest.h>

#include <string>

using namespace baker;
using namespace baker::experiments;

TEST_CASE("every corpus file survives the round trip") {
    const auto failures = round_trip_corpus(syntax::print, syntax::read);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        // A file Baker cannot read alone is not the experiment's fault; the rest are.
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
}

TEST_CASE("a converted sample reads back to the same tree") {
    auto original = syntax::read(
        "(menu ('bacon and eggs' (price 12.75) (calories 120)))\n"
        "(rules {func scaled ($base) {* $base $difficulty}})\n",
        "sample.dta");
    REQUIRE(original);
    const std::string converted = syntax::print(**original);
    auto back = syntax::read(converted, "sample.new");
    REQUIRE(back);
    CHECK(first_difference(**original, **back).empty());
}

TEST_CASE("an error is located") {
    auto r = syntax::read("(menu\n   (price 12.75)\n", "broken.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "broken.dta");
    CHECK(r.error().line() == 1);
}
