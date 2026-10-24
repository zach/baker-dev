// The experiment's tests: the corpus round trip, the migration's forms, the placement rule,
// and, with -DBAKER_AT_METADATA=ON, what the core reads differently: values that end before
// the metadata, tweaks and shapes that find it by the sigil, the paths that reach it, and
// the write-back that keeps it. The cases under `if constexpr (kAtMetadata)` are skipped,
// not failed, with the switch off.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/tweak.hpp>
#if BAKER_AT_SHAPE
#include <baker/shape.hpp>
#endif
#if BAKER_AT_SCRIPT
#include <baker/script.hpp>
#include <baker/std_script.hpp>
#endif

#include <doctest.h>

#include "temp_dir.hpp"

#include <array>
#include <string>
#include <vector>

using namespace baker;
using namespace baker::experiments;

namespace {

Ref<Array> parse(std::string_view text) {
    auto r = syntax::read(text, "t.dta");
    if (!r) FAIL(describe(r.error()));
    return *r;
}

std::string migrated(std::string_view text) {
    auto r = read_string(text, "t.dta");
    if (!r) FAIL(describe(r.error()));
    return syntax::print(**r);
}

std::string flat(const Array& a) {
    std::string out;
    for (const Node& n : a) out += (out.empty() ? "" : " ") + to_string(n);
    return out;
}

}  // namespace

TEST_CASE("every corpus file survives the round trip") {
    // Baker's reader -> print (the migration) -> read -> strip the sigils -> the original.
    const auto failures = round_trip_corpus(syntax::print, syntax::read_as_dta);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
}

TEST_CASE("the migration is idempotent and reads back with the sigils") {
    for (const std::string& rel : corpus_files()) {
        auto original = read_dta_file(std::string(repository_root()) + "/" + rel);
        if (!original) continue;
        const std::string once = syntax::print(**original);
        auto back = syntax::read(once, rel);
        REQUIRE(back);
        CHECK(syntax::print(**back) == once);
        // Nothing in the corpus carries the sigil already (D-entry gate).
        CHECK(syntax::count_meta_tags(**original) == 0);
    }
}

TEST_CASE("D218's rule names the metadata, and only after a value") {
    CHECK(migrated("(hit_pause 6 (range 0 30) (desc \"d\"))") ==
          "(hit_pause 6 (@range 0 30) (@desc \"d\"))\n");
    CHECK(migrated("(color 0.9 0.5 0.2 (range 0 1) (desc \"Cube color\"))") ==
          "(color 0.9 0.5 0.2 (@range 0 1) (@desc \"Cube color\"))\n");
    // A record with no value keeps its fields: the D218 ambiguity is reported, not decided.
    CHECK(migrated("(ball (kind ball) (pos -6 6.5))") == "(ball (kind ball) (pos -6 6.5))\n");
    CHECK(migrated("(limits (min 0) (max 100) (step 0.5))") ==
          "(limits (min 0) (max 100) (step 0.5))\n");
    // Only the trailing run: a tagged element before a value is the value's own.
    CHECK(migrated("(parts 1 (x 2) (range 0 1))") == "(parts 1 (x 2) (@range 0 1))\n");
    CHECK(migrated("(parts (range 0 1) 1)") == "(parts (range 0 1) 1)\n");
    // A field named desc on a record stays a field; the shape-level words get the sigil.
    CHECK(migrated("(shapes (household (at households) (keyed) (desc string)"
                   " (size (list float) (desc \"x z\")) (walls any (optional))))") ==
          "(shapes\n   (household\n      (@at households)\n      (@keyed)\n      (desc string)\n"
          "      (size (list float) (@desc \"x z\"))\n      (walls any         (@optional))))\n");
    CHECK(migrated("{func boss.find_cover ($player) (status stub) (doc \"d\") {nearest_marker}}") ==
          "{func boss.find_cover ($player) (@status stub) (@doc \"d\") {nearest_marker}}\n");
    // Already migrated text is left alone.
    CHECK(migrated("(x 5 (@range 0 10) (foo 1))") == "(x 5 (@range 0 10) (foo 1))\n");
}

TEST_CASE("the sigil strips back to today's tree") {
    auto a = parse(
        "(x 5 (@range 0 10) (@desc \"d\")) (shapes (s (@at a) (@keyed) (f int (@optional))))");
    auto b = syntax::strip_sigils(*a);
    CHECK(flat(*b) ==
          "(x 5 (range 0 10) (desc \"d\")) (shapes (s (at a) (keyed) (f int (optional))))");
    CHECK(syntax::count_meta_tags(*a) == 5);
    CHECK(syntax::count_meta_tags(*b) == 0);
}

TEST_CASE("metadata comes after the value: a located error at the child") {
    auto r = syntax::read("(box\n   (x (@range 0 1) 5))\n", "t.dta");
    REQUIRE(!r);
    CHECK(r.error().code() == ErrorCode::Syntax);
    CHECK(r.error().line() == 2);
    CHECK(r.error().column() == 20);
    CHECK(std::string(to_string(r.error())).find("metadata comes after the value") !=
          std::string::npos);
    // A command after metadata is a value too; a tagged child is not.
    CHECK(!syntax::read("(goal (@desc \"d\") {puzzle.in ball cup})", "t.dta"));
    CHECK(syntax::read("(move (@at fighters * moves) (@keyed) (button symbol (@one_of a b)))",
                       "t.dta"));
    CHECK(syntax::read("(household (@desc \"d\") (desc string) (size (list float)))", "t.dta"));
    CHECK(syntax::read("(cottage (desc \"Two rooms\") (size 14 10) (@note \"why\"))", "t.dta"));
    // Slot 0 is the tag's place: an array there is a value, not metadata.
    CHECK(syntax::read("((@x 1) 2)", "t.dta"));
    // Inside a {command}, forms are positional; the rule is for parenthesized arrays.
    CHECK(syntax::read("{func f () (@status stub) {body}}", "t.dta"));
    if constexpr (kAtMetadata) {  // the boundary has 14 bits: metadata past child 16383 is refused
        std::string big = "(big";
        for (int i = 0; i < 16382; ++i) big += " 1";  // (@range) at child 16383
        CHECK(read_string(big + " (@range 0 1))", "big.dta"));
        auto past = read_string(big + " 1 (@range 0 1))", "big.dta");
        REQUIRE(!past);
        CHECK(past.error().code() == ErrorCode::Limit);
    }
    if constexpr (kAtMetadata) {  // the core's own parser refuses it alike, with the column
        auto c = read_string("(box\n   (x (@range 0 1) 5))\n", "core.dta");
        REQUIRE(!c);
        CHECK(c.error().line() == 2);
        CHECK(c.error().column() == 20);
        CHECK(c.error().file().str() == "core.dta");
    }
}

TEST_CASE("an error is located") {
    auto r = syntax::read("(menu\n   (price 12.75)\n", "broken.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "broken.dta");
    CHECK(r.error().line() == 1);
}

TEST_CASE("the printer puts one-line siblings' metadata in one column, whitespace only") {
    const std::string out = migrated(
        "(cube (spin 45 (range -720 720) (step 5) (desc \"Degrees per second\"))"
        " (size 1 (range 0.1 5) (step 0.1) (desc \"Edge, in meters\"))"
        " (color 0.9 0.5 0.2 (range 0 1) (desc \"Cube color\")) (name \"cube\")"
        " (tilt 20 (range -90 90) (desc \"Degrees about x\")))");
    CHECK(out ==
          "(cube\n"
          "   (spin 45           (@range -720 720) (@step 5) (@desc \"Degrees per second\"))\n"
          "   (size 1            (@range 0.1 5) (@step 0.1) (@desc \"Edge, in meters\"))\n"
          "   (color 0.9 0.5 0.2 (@range 0 1) (@desc \"Cube color\"))\n"
          "   (name \"cube\")\n"
          "   (tilt 20           (@range -90 90) (@desc \"Degrees about x\")))\n");
    // A broken array, a line the width would overflow, and a lone row are left alone.
    CHECK(migrated("(a (x 1 (range 0 1)))") == "(a (x 1 (@range 0 1)))\n");
    auto back = syntax::read(out, "t.dta");
    REQUIRE(back);
    auto plain = read_string(out, "t.dta");
    REQUIRE(plain);
    CHECK(first_difference(**back, **plain).empty());
}

TEST_CASE("each sample reads under the rule and strips back to its original") {
    const std::string dir =
        std::string(repository_root()) + "/experiments/syntax-at-metadata/samples/";
    // The text without its #require line: a sample's pack is not beside it here.
    const auto text_of = [&](const std::string& file) {
        auto t = read_text_file(dir + file);
        REQUIRE(t);
        std::string s = *t;
        if (const size_t at = s.find("#require"); at != std::string::npos)
            s.erase(at, s.find('\n', at) - at);
        return s;
    };
    for (const char* name : {"cube", "duel", "duel-duel_core", "contraption", "hearth-hearth_core",
                             "harness-imgui", "scripts"}) {
        INFO(std::string(name));
        auto before = syntax::read(text_of(std::string(name) + ".before.dta"), "before.dta");
        REQUIRE(before);
        auto after = syntax::read_as_dta(text_of(std::string(name) + ".after.dta"), "after.dta");
        REQUIRE(after);
        const std::string diff = first_difference(**before, **after);
        if (std::string_view(name) == "hearth-hearth_core")  // the one addition: a shape's own
            CHECK(diff.find("(desc \"A floor plan and who lives on it\")") != std::string::npos);
        else
            CHECK(diff.empty());
    }
#if BAKER_AT_SHAPE
    if constexpr (kAtMetadata) {  // the packs' shapes load, a shape's own (@desc) included
        for (const char* name : {"duel-duel_core", "hearth-hearth_core", "harness-imgui"}) {
            INFO(std::string(name));
            auto tree = syntax::read(text_of(std::string(name) + ".after.dta"), "after.dta");
            REQUIRE(tree);
            shape::Set set;
            shape::Report report;
            set.add(**tree, report);
            CHECK(report.errors.empty());
        }
        auto harness = syntax::read(text_of("harness-imgui.after.dta"), "after.dta");
        REQUIRE(harness);
        CHECK((*harness)->get<std::array<float, 3>>("scene.ball.color"));
        CHECK(*(*harness)->get<std::string_view>("scene.ball.bounce.@note") == "1.0 never settles");
    }
#endif
}

// --- With the switch on: what the core reads differently ---

TEST_CASE("the converted cube file reads its color as three floats; the original does not") {
    const std::string path = std::string(repository_root()) + "/stacks/cube/data/default.dta";
    auto original = read_dta_file(path);
    REQUIRE(original);
    auto converted = syntax::read(syntax::print(**original), path);
    REQUIRE(converted);
    if constexpr (kAtMetadata) {
        auto color = (*converted)->get<std::array<float, 3>>("cube.color");
        REQUIRE(color);
        CHECK((*color)[0] == 0.9f);
        CHECK(*(*converted)->get<std::string_view>("cube.color.@desc") == "Cube color");
        CHECK(*(*converted)->get<float>("cube.spin.@range.1") == 720.0f);
        CHECK(!(*converted)->get<float>("cube.spin.@range.2"));  // indices count values from 0
        CHECK(!(*original)->get<std::array<float, 3>>("cube.color"));
        // A binary cache keeps the boundary: it rides in the array header (D143's way).
        std::string bytes;
        REQUIRE(write_cache(**converted, string_out(bytes)));
        auto cached = read_cache_bytes(bytes, "cube.datb");
        REQUIRE(cached);
        const Array& color_array = *(*cached)->try_get(Path("cube.color"));
        CHECK(color_array.meta_first() == 4);
        CHECK(color_array.values().size() == 3);
        CHECK((*cached)->get<std::array<float, 3>>("cube.color"));
    } else {  // the switch off: every reader sees five children, as today's text gives
        CHECK(!(*converted)->get<std::array<float, 3>>("cube.color"));
        CHECK((*converted)->try_get(Path("cube.color"))->values().size() == 5);
    }
}

TEST_CASE("a value ends before its metadata, for every reader") {
    if constexpr (kAtMetadata) {
        auto cube = parse("(cube (color 0.9 0.5 0.2 (@range 0 1) (@desc \"Cube color\")))");
        auto c = cube->get<std::array<float, 3>>("cube.color");
        REQUIRE(c);
        CHECK((*c)[2] == 0.2f);
        const Array& color = *cube->try_get(Path("cube.color"));
        CHECK(color.values().size() == 3);
        CHECK(color.metadata().size() == 2);
        CHECK(color.meta_first() == 4);
        REQUIRE(color.meta("@desc"_sym));
        CHECK(*(*color.meta("@desc"_sym))[1].try_str() == "Cube color");
        CHECK(color.meta("@range"_sym) == color.try_find("@range"));
        CHECK(color.meta("desc"_sym) == nullptr);
        // By path: the metadata child is a child still.
        CHECK(*cube->get<std::string_view>("cube.color.@desc") == "Cube color");
        CHECK(*cube->get<float>("cube.color.@range.1") == 1.0f);
        // The unmigrated spelling is four values: a plain child.
        auto old = parse("(cube (color 0.9 0.5 0.2 (range 0 1)))");
        CHECK(!old->get<std::array<float, 3>>("cube.color"));
        CHECK(old->try_get(Path("cube.color"))->meta_first() == 0);
    }
}

TEST_CASE("a field named kind, desc or at is a field, in any order") {
    if constexpr (kAtMetadata) {
        for (const char* text : {"(pieces (ball (kind ball) (pos -6 6.5)))",
                                 "(pieces (ball (pos -6 6.5) (kind ball)))"}) {
            auto t = parse(text);
            CHECK(*t->get<Symbol>("pieces.ball.kind") == "ball"_sym);
            CHECK(t->try_get(Path("pieces.ball"))->values().size() == 2);
            const ValueMetadata vm = split_metadata(t->try_get(Path("pieces.ball"))->values());
            CHECK(vm.values.size() == 2);
            CHECK(vm.metadata.empty());
        }
        auto w = parse("(box (width 2 (@range 0 10) (@desc \"w\")))");
        const ValueMetadata vm = split_metadata(w->try_get(Path("box.width"))->values());
        CHECK(vm.values.size() == 1);
        CHECK(vm.metadata.size() == 2);
        CHECK(presented_kind(w->try_get(Path("box.width"))->values()) == "");
        auto tint = parse("(look (tint 0.9 0.5 0.2 (@range 0 1)))");
        CHECK(presented_kind(tint->try_get(Path("look.tint"))->values()) == "color");
    }
}

TEST_CASE("a tweak reads its metadata by the sigil, and the old spelling warns") {
    if constexpr (kAtMetadata) {
        TweakRegistry reg;
        Tweak<float> x{"box.x", 1.0f, {}, reg};
        Tweak<float> y{"box.y", 1.0f, {}, reg};
        Tweak<std::array<float, 3>> tint{"box.tint", {1, 1, 1}, {}, reg};
        Config cfg({.tweaks = &reg});
        REQUIRE(cfg.push_layer("default", parse("(box (x 5 (@range 0 10) (@step 2) (@desc \"d\")"
                                                " (@note \"n\") (@kind frames))\n"
                                                " (y 5 (range 0 10))"
                                                " (tint 0.9 0.5 0.2 (@range 0 1)))")));
        CHECK(float(x) == 5.0f);
        CHECK(x.range().min == 0);
        CHECK(x.range().max == 10);
        CHECK(x.step() == 2);
        CHECK(x.desc() == "d");
        CHECK(x.note() == "n");
        CHECK(x.kind() == "frames");
        CHECK(float(y) == 5.0f);
        CHECK(y.range().empty());  // (range 0 10) is a field of y now
        CHECK(tint.kind() == "color");
        CHECK(tint.value()[2] == 0.2f);
        // check_tweaks holds the data to the data's own range.
        REQUIRE(cfg.push_layer("over", parse("(box (x 50))")));
        const TweakFindings f = check_tweaks(*cfg.root(), reg);
        REQUIRE(f.errors.size() == 1);
        CHECK(std::string(to_string(f.errors[0])).find("outside its tweak's range") !=
              std::string::npos);
        // The migration warning: a bare (range ...) after a value, once per layer.
        auto report =
            cfg.check_layer("candidate", parse("(box (y 6 (range 0 10)) (z 1 (desc \"z\")))"));
        REQUIRE(report);
        REQUIRE(report->warnings.size() == 1);
        CHECK(std::string(to_string(report->warnings[0])).find("write (@range ...)") !=
              std::string::npos);
        auto clean = cfg.check_layer("clean", parse("(box (y 6 (@range 0 10)))"));
        REQUIRE(clean);
        CHECK(clean->warnings.empty());
    }
}

TEST_CASE("a layer restating a value keeps the metadata below; sorted mode keeps the boundary") {
    if constexpr (kAtMetadata) {
        auto r = merge(*parse("(width 2 (@range 0 10) (@desc \"w\"))"), *parse("(width 3)"));
        CHECK(flat(*r) == "(width 3 (@range 0 10) (@desc \"w\"))");
        CHECK(r->try_find("width")->values().size() == 1);
        CHECK(r->try_find("width")->metadata().size() == 2);
        // A layer removes one metadata child by its tag, as any key (D143).
        auto removed = merge(*r, *parse("(width (@range #remove))"));
        CHECK(flat(*removed) == "(width 3 (@desc \"w\"))");
        CHECK(removed->try_find("width")->metadata().size() == 1);
        // Sorted: untagged first, then the fields sorted, then the metadata sorted.
        auto s = parse("(rec 1 2 (zeta 1) (@zz 1) (alpha 2) (@aa 2) (mid 3))");
        Array& rec = const_cast<Array&>(*s->try_find("rec"));
        rec.sort_for_lookup();
        CHECK(rec.values().size() == 5);
        CHECK(rec.metadata().size() == 2);
        CHECK(rec[1].try_int() == 1);
        CHECK(rec[2].try_int() == 2);
        CHECK(!rec[3].try_array()->tag().is_meta());
        CHECK(!rec[5].try_array()->tag().is_meta());
        CHECK(rec[6].try_array()->tag().is_meta());
        CHECK(rec.meta_first() == 6);
        CHECK(*rec.find_int("mid") == 3);
        CHECK(*rec.find_int("alpha") == 2);
        CHECK(*rec.find_int("@aa") == 2);
        CHECK(*rec.find_int("@zz") == 1);
        CHECK(rec.try_find("nope") == nullptr);
        CHECK(rec.try_find("@nope") == nullptr);
        // set() moves the boundary when the slot is, or becomes, the first metadata child.
        auto t = parse("(x 1 2 (@a 1))");
        Array& x = const_cast<Array&>(*t->try_find("x"));
        CHECK(x.meta_first() == 3);
        x.set(3, Node(array("b", 2)));  // no longer metadata
        CHECK(x.meta_first() == 0);
        CHECK(x.values().size() == 3);
        x.set(2, Node(array("@c", 3)));
        CHECK(x.meta_first() == 2);
        CHECK(x.values().size() == 1);
        x.set(1, Node(array("@d", 3)));
        CHECK(x.meta_first() == 1);
        CHECK(x.values().empty());
    }
}

TEST_CASE("write-back grows a value past its metadata, and inserts after it") {
    if constexpr (kAtMetadata && !kShipping) {
        TweakRegistry reg;
        Tweak<std::vector<float>> parts{"box.parts", {}, {}, reg};
        baker::test::TempDir dir;
        Config cfg({.tweaks = &reg});
        REQUIRE(cfg.push_layer(
            "default",
            dir.write("default.dta", "(box\n   (parts 1.5 2.5 (@range 0 9) (@note \"n\")))\n")));
        REQUIRE(parts.get() == std::vector<float>{1.5f, 2.5f});
        REQUIRE(parts.set({1.5f, 2.5f, 3.5f}));
        REQUIRE(parts.write_back());
        CHECK(dir.read("default.dta") ==
              "(box\n   (parts 1.5 2.5 3.5 (@range 0 9) (@note \"n\")))\n");
        REQUIRE(cfg.set("box.parts.extra", Node(int64_t{7})));
        REQUIRE(cfg.write_back_to("box.parts.extra", "default"));
        const std::string text = dir.read("default.dta");
        CHECK(text == "(box\n   (parts 1.5 2.5 3.5 (@range 0 9) (@note \"n\") (extra 7)))\n");
        auto again = syntax::read(text, "default.dta");  // tagged children may follow metadata
        REQUIRE(again);
        CHECK(*(*again)->get<int>("box.parts.extra") == 7);
        CHECK((*again)->try_get(Path("box.parts"))->values().size() == 3);
    }
}

#if BAKER_AT_SHAPE
TEST_CASE("a shape's (@at ...) and (@keyed) govern; a field may be named at or kind") {
    if constexpr (kAtMetadata) {
        auto tree = parse(
            "(shapes\n"
            "   (move (@at fighters * moves) (@keyed) (@desc \"A move\")\n"
            "      (button symbol (@one_of light heavy))\n"
            "      (startup int (@range 1 60) (@kind frames) (@desc \"Frames\"))\n"
            "      (at symbol (@optional))\n"
            "      (kind symbol (@optional))))\n"
            "(fighters (kestrel (moves (jab (button light) (startup 4) (at home) (kind quick))\n"
            "                          (bad (button nope) (startup 99)))))\n");
        shape::Set set;
        shape::Report report;
        set.add(*tree, report);
        REQUIRE(report.errors.empty());
        const shape::Shape* move = set.find("move");
        REQUIRE(move);
        CHECK(move->keyed);
        REQUIRE(move->at.size() == 3);
        CHECK(move->at[1] == "*"_sym);
        REQUIRE(move->field("startup"));
        CHECK(move->field("startup")->ranged);
        CHECK(move->field("startup")->kind == "frames"_sym);
        CHECK(move->field("startup")->desc == "Frames");
        CHECK(move->field("at"));
        CHECK(move->field("kind"));
        CHECK(move->field("at")->optional);
        shape::check(*tree, set, report);
        REQUIRE(report.errors.size() == 2);
        CHECK(std::string(to_string(report.errors[0])).find("'button' is 'nope', not one of") !=
              std::string::npos);
        CHECK(std::string(to_string(report.errors[1])).find("'startup' is 99, outside") !=
              std::string::npos);
        CHECK(report.warnings.empty());
        // The old spelling is a field now, and an unknown @word is an error, not silence.
        shape::Report bad;
        shape::Set bad_set;
        bad_set.add(*parse("(shapes (s (@at a) (x int (range 0 1))))"), bad);
        REQUIRE(bad.errors.size() == 1);
        CHECK(std::string(to_string(bad.errors[0])).find("write (@range ...)") !=
              std::string::npos);
        shape::Report typo;
        shape::Set typo_set;
        typo_set.add(*parse("(shapes (s (@at a) (x int (@rnage 0 1))))"), typo);
        REQUIRE(typo.errors.size() == 1);
        CHECK(std::string(to_string(typo.errors[0])).find("unknown metadata '@rnage'") !=
              std::string::npos);
        shape::Report shape_typo;
        shape::Set shape_typo_set;
        shape_typo_set.add(*parse("(shapes (s (@keyd) (@at a) (x int)))"), shape_typo);
        REQUIRE(shape_typo.errors.size() == 1);
        CHECK(std::string(to_string(shape_typo.errors[0])).find("unknown metadata '@keyd'") !=
              std::string::npos);
    }
}
#endif

#if BAKER_AT_SCRIPT
TEST_CASE("{func} reads (@status) and (@doc) by the sigil; (status 1) is a body") {
    if constexpr (kAtMetadata) {
        Context ctx;
        auto def = ctx.eval("{func f () (@status stub) (@doc \"the doc\") (status 1)}", "t.dta");
        REQUIRE(def);
        auto r = ctx.eval("{f}", "t.dta");
        REQUIRE(r);
        CHECK(flat(*r->array()) == "status 1");  // the body's one form, an array, returned as is
        auto bad = ctx.eval("{func g () (@stauts stub) 1}", "t.dta");
        REQUIRE(!bad);
        CHECK(std::string(to_string(bad.error())).find("unknown metadata '@stauts'") !=
              std::string::npos);
    }
}
#endif
