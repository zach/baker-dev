// The experiment's tests: the corpus round trip, a case for every form, and the errors.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <doctest.h>

#include <random>
#include <string>

using namespace baker;
using namespace baker::experiments;

namespace {

Ref<Array> rows(std::string_view text) {
    auto r = syntax::read(text, "rows.dta");
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : std::string(describe(r.error()))));
    return *r;
}
Ref<Array> dta(std::string_view text) {
    auto r = read_string(text, "nested.dta");
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : std::string(describe(r.error()))));
    return *r;
}
Error error(std::string_view text) {
    auto r = syntax::read(text, "rows.dta");
    REQUIRE_MESSAGE(!r.has_value(), "expected an error for: " << text);
    return r.error();
}
// Rows read to the same tree as their nested spelling.
void same(std::string_view row_text, std::string_view nested_text) {
    const Ref<Array> a = rows(row_text);
    const Ref<Array> b = dta(nested_text);
    INFO(first_difference(*a, *b));
    CHECK(first_difference(*a, *b).empty());
}

}  // namespace

TEST_CASE("every corpus file survives the round trip") {
    const auto failures =
        round_trip_corpus([](const Array& r) { return syntax::print(r); }, syntax::read);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        // A file Baker cannot read alone is not the experiment's fault; the rest are.
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
}

TEST_CASE("the brief's five rows read as their nested forms") {
    same(
        "(/fighters.kestrel.health 1100)   ; the turtle wins too often\n"
        "(/fighters.kestrel.walk_back 1.7)\n"
        "(/cpu.easy.reaction 26)\n",
        "(fighters (kestrel (health 1100) (walk_back 1.7)))\n(cpu (easy (reaction 26)))\n");
    same("(/fighters.kestrel.moves.jab.startup 5)",
         "(fighters (kestrel (moves (jab (startup 5)))))");
    same("(/event.xp_multiplier 2)\n(/event.banner \"Double XP all weekend!\")\n",
         "(event (xp_multiplier 2) (banner \"Double XP all weekend!\"))");
    same(
        "(/shapes.boss_phase.enter_at float (range 0 2))\n"
        "(/shapes.boss.taunts string (optional))\n",
        "(shapes (boss_phase (enter_at float (range 0 2))) (boss (taunts string (optional))))");
    same("(/player.speed 4.5)", "(player (speed 4.5))");
}

TEST_CASE("a row's body is an array body: values, metadata, children, commands") {
    same("(/a.b 1 2 3)", "(a (b 1 2 3))");
    same("(/a.b 2 (range 0 10) (desc \"width\"))", "(a (b 2 (range 0 10) (desc \"width\")))");
    same("(/a.b (c 1) (d 2))", "(a (b (c 1) (d 2)))");
    same("(/entry.on_hit {play_sfx \"hit.wav\"} {game add_score 10})",
         "(entry (on_hit {play_sfx \"hit.wav\"} {game add_score 10}))");
    same("(/a.b)", "(a (b))");
    same("(/a 1)", "(a 1)");
}

TEST_CASE("the row's file and line are on every array it makes") {
    const Ref<Array> root = rows("; a comment\n\n(/fighters.kestrel.moves.jab.startup 5)\n");
    const Array* a = &*root;
    for (const char* tag : {"fighters", "kestrel", "moves", "jab", "startup"}) {
        a = a->try_find(tag);
        REQUIRE(a);
        CHECK(a->line() == 3);
        CHECK(a->file() == Symbol("rows.dta"));
    }
    CHECK((*a)[1].try_int() == 5);
}

TEST_CASE("rows under one prefix fold into one array per tag, in the written order") {
    same("(/a.b 1)\n(/a.c 2)\n(/a.d.e 3)\n(/a.d.f 4)\n", "(a (b 1) (c 2) (d (e 3) (f 4)))");
    // A row and a nested literal sharing a tag are joined too.
    same("(fighters (kestrel (health 1000)))\n(/fighters.kestrel.walk_back 1.7)\n",
         "(fighters (kestrel (health 1000) (walk_back 1.7)))");
    same("(/fighters.kestrel.walk_back 1.7)\n(fighters (kestrel (health 1000)))\n",
         "(fighters (kestrel (walk_back 1.7) (health 1000)))");
    // The parent's own values, then its children, as written.
    same("(/a 1)\n(/a.b 2)\n", "(a 1 (b 2))");
    same("(/a.b 2)\n(/a 1)\n", "(a (b 2) 1)");
    // The first occurrence keeps its place among the top-level forms.
    same("(/a.x 1)\n(z 0)\n(/a.y 2)\n", "(a (x 1) (y 2))\n(z 0)\n");
    // Without a row, duplicates are left as D31 leaves them.
    same("(a 1)\n(a 2)\n", "(a 1)\n(a 2)\n");
}

TEST_CASE("the folded tree is what layer_effect and diff see: one array per key") {
    const Ref<Array> root =
        rows("(/fighters.kestrel.health 1100)\n(/fighters.kestrel.walk_back 1.7)\n");
    REQUIRE(root->size() == 1);
    const Array& kestrel = *root->try_find("fighters")->try_find("kestrel");
    CHECK(kestrel.size() == 3);
    CHECK(*kestrel.find_int("health") == 1100);
    CHECK(*kestrel.find_float("walk_back") == doctest::Approx(1.7));
    CHECK(kestrel.try_find("health")->line() == 1);
    CHECK(kestrel.try_find("walk_back")->line() == 2);
}

TEST_CASE("a leaf written twice is a located error at the second writing") {
    Error e = error("(/fighters.kestrel.health 1100)\n(/fighters.kestrel.health 1200)\n");
    CHECK(e.message() == "'fighters/kestrel/health' is written twice");
    CHECK(e.file() == Symbol("rows.dta"));
    CHECK(e.line() == 2);
    CHECK(e.column() == 2);
    e = error("(fighters (kestrel (health 1000)))\n(/fighters.kestrel.health 1100)\n");
    CHECK(e.message() == "'fighters/kestrel/health' is written twice");
    CHECK(e.line() == 2);
    e = error("(/fighters.kestrel.health 1100)\n(fighters (kestrel (health 1000)))\n");
    CHECK(e.message() == "'fighters/kestrel/health' is written twice");
    CHECK(e.line() == 2);
    // Values on the parent and on a child are two leaves.
    same("(/a 1)\n(/a.b 2)\n", "(a 1 (b 2))");
}

TEST_CASE("#remove in a row is D143's removal") {
    const Ref<Array> root = rows("(/menu.debug_menu #remove)");
    const Array& menu = *root->try_find("menu");
    REQUIRE(menu.try_find("debug_menu"));
    CHECK(menu.try_find("debug_menu")->size() == 2);
    CHECK((*menu.try_find("debug_menu"))[1].is(Symbol("#remove")));
    CHECK(menu.has_removal());
    CHECK(root->has_removal());
    same("(/menu.debug_menu #remove)", "(menu (debug_menu #remove))");
    // As a layer, it removes the key below (merge is untouched: it sees the nested tree).
    const Ref<Array> below = dta("(menu (debug_menu 1) (volume 3))");
    const Ref<Array> merged = merge(*below, *root, MergePolicy::Overwrite);
    CHECK(!merged->try_find("menu")->try_find("debug_menu"));
    CHECK(merged->try_find("menu")->try_find("volume"));
    // A removed key written as well is an error.
    const Error e = error("(/menu.debug_menu #remove)\n(/menu.debug_menu.x 1)\n");
    CHECK(e.message() == "'menu/debug_menu' is removed and also written");
    CHECK(e.line() == 2);
    // The marker keeps its own rule.
    CHECK(error("(/menu.debug_menu #remove 1)").message().find("#remove") != std::string::npos);
}

TEST_CASE("quoted segments name keys holding a separator or digits") {
    same("(/menu.'v1.2'.price 12)", "(menu ('v1.2' (price 12)))");
    same("(/a.'b/c' 1)", "(a ('b/c' 1))");
    same("(/a.'42' 1)", "(a ('42' 1))");
    same("(/'1.0'.x 1)", "('1.0' (x 1))");
    // '/' separates too, as a Path's does (D27).
    same("(/a/b/c 1)", "(a (b (c 1)))");
}

TEST_CASE("the key's errors are located at the row") {
    Error e = error("(a 1)\n  (/fighters.0.health 5)\n");
    CHECK(e.message().find("'0' is an element") != std::string::npos);
    CHECK(e.line() == 2);
    CHECK(e.column() == 4);
    e = error("(/a..b 1)");
    CHECK(e.message() == "the key has an empty segment");
    CHECK(e.column() == 2);
    e = error("(/a.b/ 1)");
    CHECK(e.message() == "the key ends with a separator");
    e = error("(/a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q 1)");
    CHECK(e.message() == "the key has more than 16 segments");
    same("(/a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p 1)",
         "(a (b (c (d (e (f (g (h (i (j (k (l (m (n (o (p 1))))))))))))))))");
    e = error("(/a.'b 1)");
    CHECK(e.message() == "the key has an unclosed quote");
    e = error("(/a.'' 1)");
    CHECK(e.message() == "the key has an empty segment");
}

TEST_CASE("a row inside any bracket is an error; a quoted or lone '/' is a symbol") {
    Error e = error("(fighters\n   (/kestrel.health 5))\n");
    CHECK(e.message().find("a row is a top-level form") != std::string::npos);
    CHECK(e.line() == 2);
    CHECK(e.column() == 5);
    e = error("{f (/a.b 1)}");
    CHECK(e.line() == 1);
    // A '/'-led symbol anywhere else is the symbol it always was.
    same("(sep / \\ ,)", "(sep / \\ ,)");
    same("(/ 1)", "(/ 1)");
    same("(path /usr/share/game)", "(path /usr/share/game)");
    same("{/a.b 1}", "{/a.b 1}");
    same("('/x' 1)", "('/x' 1)");
    same("(a ('/x' 1))", "(a ('/x' 1))");
    // The printer writes a '/'-led tag as a quoted segment, or quoted where it heads a
    // nested form, so it reads back as the symbol.
    CHECK(syntax::print(*dta("('/x' 1)")) == "(/'/x' 1)\n");
    CHECK(syntax::print(*dta("(a ('/x' 1))")) == "(/a.'/x' 1)\n");
    CHECK(syntax::print(*dta("('/x' 1)\n('/x' 2)\n")) == "('/x' 1)\n('/x' 2)\n");
    CHECK(syntax::print(*dta("(a ('/x' 1))\n(a 2)\n")) == "(a ('/x' 1))\n(a 2)\n");
}

TEST_CASE("the printer writes one row per leaf and keeps metadata and bare tags in the body") {
    CHECK(syntax::print(*dta("(fighters (kestrel (health 1100) (walk_back 1.7)))"
                             "(cpu (easy (reaction 26)))")) ==
          "(/fighters.kestrel.health 1100)\n(/fighters.kestrel.walk_back 1.7)\n"
          "(/cpu.easy.reaction 26)\n");
    CHECK(syntax::print(*dta("(shapes (boss_phase (enter_at float (range 0 2)))"
                             " (boss (taunts string (optional))))")) ==
          "(/shapes.boss_phase.enter_at float (range 0 2))\n"
          "(/shapes.boss.taunts string (optional))\n");
    CHECK(syntax::print(*dta("(menu (debug_menu #remove))")) == "(/menu.debug_menu #remove)\n");
    CHECK(syntax::print(*dta("(player (speed 4.5))")) == "(/player.speed 4.5)\n");
    // A parent with values and children: its row, then theirs.
    CHECK(syntax::print(*dta("(a 1 (b 2))")) == "(/a 1)\n(/a.b 2)\n");
    // A child before a value, or a repeated child, keeps the body nested, and so rereads.
    CHECK(syntax::print(*dta("(a (b 2) 1)")) == "(/a (b 2) 1)\n");
    CHECK(syntax::print(*dta("(a (b 1) (b 2))")) == "(/a (b 1) (b 2))\n");
    CHECK(syntax::print(*dta("(limits (min 0) (max 100) (step 0.5))")) ==
          "(/limits (min 0) (max 100) (step 0.5))\n");
    // A repeated top-level tag is a list (D31): the file stays DTA.
    CHECK(syntax::print(*dta("(phase (t 1))\n(phase (t 2))\n")) ==
          "(phase (t 1))\n(phase (t 2))\n");
    // A key that cannot be a segment heads a nested form.
    CHECK(syntax::print(*dta("('bacon and eggs' (price 12))")) ==
          "('bacon and eggs' (price 12))\n");
    CHECK(syntax::print(*dta("(menu ('bacon and eggs' (price 12)))")) ==
          "(/menu ('bacon and eggs' (price 12)))\n");
}

TEST_CASE("1,000 generated leaf sets read the same nested and as rows") {
    std::mt19937 rng(7);
    const char* names[] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    for (int set = 0; set < 1000; ++set) {
        // A random tree of unique tags per level, values at the leaves and sometimes inside.
        std::string nested;
        auto gen = [&](auto& self, int depth, std::string& out) -> void {
            const int n = 1 + static_cast<int>(rng() % 4);
            for (int i = 0; i < n; ++i) {
                out += '(';
                out += names[i];
                const bool leaf = depth >= 3 || rng() % 3 == 0;
                if (leaf || rng() % 4 == 0) out += " " + std::to_string(rng() % 100);
                if (!leaf) {
                    out += ' ';
                    self(self, depth + 1, out);
                }
                out += ')';
            }
        };
        gen(gen, 0, nested);
        const Ref<Array> tree = dta(nested);
        const std::string as_rows = syntax::print(*tree);
        auto back = syntax::read(as_rows, "rows.dta");
        REQUIRE_MESSAGE(back.has_value(), describe(back.error()) << "\n" << as_rows);
        INFO(nested << "\n" << as_rows);
        CHECK(first_difference(*tree, **back).empty());
        // And the nested text reads the same through the rows reader (DTA is a subset).
        auto plain = syntax::read(nested, "rows.dta");
        REQUIRE(plain.has_value());
        CHECK(first_difference(*tree, **plain).empty());
    }
}

TEST_CASE("an error is located") {
    auto r = syntax::read("(menu\n   (price 12.75)\n", "broken.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "broken.dta");
    CHECK(r.error().line() == 1);
}
