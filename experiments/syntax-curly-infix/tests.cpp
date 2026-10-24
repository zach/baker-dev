// The experiment's tests: the corpus round trip, the converter on real files, each form, and
// the errors the reader reports. The core's own tests of the rule are in tests/data
// (test_parse.cpp, test_write.cpp, test_cache.cpp) and tests/script (test_eval.cpp,
// test_check.cpp, test_object.cpp).
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <doctest.h>

#include <string>

using namespace baker;
using namespace baker::experiments;

namespace {

std::string root_path(std::string_view rel) {
    return std::string(repository_root()) + "/" + std::string(rel);
}

const syntax::Converted& converted(std::string_view rel) {
    static std::string last;
    static syntax::Converted c;
    if (last != rel) {
        auto r = syntax::to_new(root_path(rel));
        REQUIRE(r);
        c = std::move(*r);
        last = std::string(rel);
    }
    return c;
}

bool has_line(const std::string& text, std::string_view line) {
    return text.find(line) != std::string::npos;
}

}  // namespace

TEST_CASE("every corpus file survives the round trip, every command that can be infix printed so") {
    const auto failures = round_trip_corpus(syntax::print, syntax::read);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        // A file Baker cannot read alone is not the experiment's fault; the rest are.
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
}

TEST_CASE("the converter's text reads back to its tree, marks included, for every corpus file") {
    int changed = 0;
    for (const std::string& rel : corpus_files()) {
        if (!read_dta_file(root_path(rel))) continue;
        INFO(rel);
        auto c = syntax::to_new(root_path(rel));
        REQUIRE(c);
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        auto back = read_string(c->text, root_path(rel), session);
        REQUIRE(back);
        CHECK(first_difference(*c->tree, **back).empty());
        CHECK(syntax::first_mark_difference(*c->tree, **back).empty());
        changed += c->counts.top_changed;
    }
    CHECK(changed > 50);
}

TEST_CASE("a band becomes one chained comparison; a single bound reads as written") {
    const auto& c = converted("stacks/duel/metrics/duel.dta");
    CHECK(has_line(c.text, "   (pass {0.3 <= $value <= 0.7}))\n"));
    CHECK(has_line(c.text, "   (pass {$value == 0}))\n"));
    CHECK(has_line(c.text, "   (pass {$value <= 350}))\n"));
    CHECK(has_line(c.text, "   (pass {or {$value < 0} {$value >= 10}}))\n"));
    // Comments and every other byte are kept.
    CHECK(has_line(c.text, "; What \"good\" means for Duel"));
    CHECK(has_line(c.text, "   (over median)  ; a win rate moves from seed to seed"));
    CHECK(c.counts.bands >= 4);
}

TEST_CASE("bounds that cannot chain turn to read low to high, each its own comparison") {
    const auto& c = converted("stacks/stuntline/metrics/track.dta");
    CHECK(has_line(c.text, "{and {0 < $value} {$value <= {1.5 * {track.par}}}}"));
    CHECK(c.counts.ascending >= 1);
}

TEST_CASE("a nest of one operator merges into one sum, its line breaks kept") {
    const auto& c = converted("stacks/hearth/metrics/hearth.dta");
    CHECK(has_line(c.text,
                   "   (measure {{hearth.events stuck} + {hearth.events oscillating} +\n"
                   "               {hearth.events starving} + {hearth.events script_errors}})\n"));
    CHECK(c.counts.flattened == 2);
    CHECK(c.counts.reassociated == 1);  // the right-hand {+ c d}: exact for integer counts
}

TEST_CASE("the calculator: ternaries, nested; a quotient of a sum") {
    const auto& c = converted("examples/calculator/functions.dta");
    CHECK(has_line(c.text, "{func square ($x) {$x * $x}}\n"));
    CHECK(has_line(c.text, "{func average ($a $b) {{$a + $b} / 2}}\n"));
    CHECK(has_line(c.text,
                   "{func clamp ($x $lo $hi) {{$x < $lo} ? $lo : {{$x > $hi} ? $hi : $x}}}\n"));
    CHECK(c.counts.ternaries == 2);
}

TEST_CASE("an assignment of an operation on its own place becomes += and friends") {
    const auto& c = converted("tests/corpus/ui_menu.dta");
    CHECK(has_line(c.text, "{$menu_enter_count += 1}"));
    CHECK(has_line(c.text, "{while {$i < kMaxPlayers}"));
    CHECK(has_line(c.text, "{$i += 1}"));
    // A multi-line `if` is a statement: it stays prefix, its condition infix.
    CHECK(has_line(c.text, "{if {$platform == pc}\n"));
    CHECK(c.counts.assignments == 2);
}

TEST_CASE("the reader: infix and prefix are one tree, and the printers give each back") {
    auto infix = syntax::read("(pass {0.3 <= $value <= 0.7})\n{{$d > 2} ? 12 : 9}\n", "a.dta");
    auto prefix = syntax::read("(pass {<= 0.3 $value 0.7})\n{if {> $d 2} 12 9}\n", "b.dta");
    REQUIRE(infix);
    REQUIRE(prefix);
    CHECK(first_difference(**infix, **prefix).empty());
    CHECK(syntax::print_prefix(**infix) == "(pass {<= 0.3 $value 0.7})\n{if {> $d 2} 12 9}\n");
    CHECK(syntax::print(**prefix) == "(pass {0.3 <= $value <= 0.7})\n{{$d > 2} ? 12 : 9}\n");
    CHECK(to_dta(**infix) == "(pass {0.3 <= $value <= 0.7})\n{{$d > 2} ? 12 : 9}\n");
}

TEST_CASE("the printers keep a tree the reader would misread: an operator as a value is quoted") {
    auto root = syntax::read("{f '-' 1} {+ '-' 1} {wheat -> flour}", "q.dta");
    REQUIRE(root);
    CHECK(syntax::print_prefix(**root) == "{f '-' 1}\n{+ '-' 1}\n{wheat -> flour}\n");
    auto back = syntax::read(syntax::print(**root), "q2.dta");
    REQUIRE(back);
    CHECK(first_difference(**root, **back).empty());
}

TEST_CASE("mixed operators are an error located by line and column") {
    auto r = syntax::read("(pass\n   {0 < $value <= 10})", "mixed.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "mixed.dta");
    CHECK(r.error().line() == 2);
    CHECK(r.error().column() == 4);
    CHECK(r.error().message() ==
          "mixed operators in {...}: nest braces to say which applies first");
    auto t = syntax::read("{$c ? 1 : 2 : 3}", "t.dta");
    REQUIRE(!t);
    CHECK(t.error().column() == 1);
}

TEST_CASE("an unclosed brace is located, as today") {
    auto r = syntax::read("(menu\n   {1 + 2)\n", "broken.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "broken.dta");
    CHECK(r.error().line() == 2);
}

TEST_CASE("text size counts brackets, atoms and compact characters") {
    const syntax::TextSize a = syntax::text_size("{and {>= $value 0.3}\n   {<= $value 0.7}} ; x");
    CHECK(a.brackets == 6);
    CHECK(a.atoms == 7);
    CHECK(a.chars == std::string("{and {>= $value 0.3} {<= $value 0.7}}").size());
    const syntax::TextSize b = syntax::text_size("{0.3 <= $value <= 0.7}");
    CHECK(b.brackets == 2);
    CHECK(b.atoms == 5);
    CHECK(b.chars == 22);
    CHECK(syntax::text_size("(\"a b\" 'c d')").atoms == 2);
}
