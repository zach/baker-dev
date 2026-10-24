// The experiment's tests: the corpus round trip, the conversion of restated layers to the
// verbs and back, the stacks' lists, and a located error.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <doctest.h>

#include <string>

using namespace baker;
using namespace baker::experiments;

namespace {

Ref<Array> parse(const char* text) {
    auto r = read_string(text, "t.dta");
    REQUIRE(r);
    return *r;
}

// to_markers, and the claim behind it: the verbs over the base read as the restated layer
// does today. `today` false: a restated list of keyed tuples, which today's merge folds by
// key, so the verbs read what the layer says instead (checked by the caller).
std::string converted(const char* base_text, const char* layer_text, bool today = true) {
    const Ref<Array> base = parse(base_text), layer = parse(layer_text);
    const Ref<Array> verbs = syntax::to_markers(*base, *layer);
    const std::string printed = syntax::print(*verbs);
    auto back = syntax::read(printed, "verbs.dta");  // it reads back as written
    REQUIRE(back);
    CHECK(first_difference(**back, *verbs).empty());
    if (!today) return to_string(*verbs);
    CHECK(first_difference(*merge(*base, *verbs, MergePolicy::Overwrite),
                           *merge(*base, *layer, MergePolicy::Overwrite))
              .empty());
    // And resolved, a Baker without the verbs reads the same.
    const Ref<Array> plain = syntax::resolve(*base, *verbs);
    CHECK(syntax::marker_free(*plain));
    CHECK(first_difference(*merge(*base, *plain, MergePolicy::Overwrite),
                           *merge(*base, *layer, MergePolicy::Overwrite))
              .empty());
    return to_string(*verbs);
}

}  // namespace

TEST_CASE("every corpus file survives the round trip") {
    const auto failures = round_trip_corpus(syntax::print, syntax::read);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        // A file Baker cannot read alone is not the experiment's fault; the rest are.
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
    // No corpus file holds a verb: the round trip is structural identity, and nothing in
    // the corpus reads differently.
    for (const std::string& file : corpus_files()) {
        auto root = read_dta_file(std::string(repository_root()) + "/" + file);
        if (root) CHECK(syntax::marker_free(**root));
    }
}

TEST_CASE("a restated list becomes #append or #drop against the base") {
    // Wayfarer's two DLCs (content-updates.md scenario 2).
    const char* items = "(items climbing_gloves glider lamp_oil spyglass)";
    CHECK(converted(items, "(items climbing_gloves glider lamp_oil spyglass rope)") ==
          "((items #append rope))");
    CHECK(converted(items, "(items climbing_gloves lamp_oil spyglass)") ==
          "((items #drop glider))");
    // A list that loses some and gains one: two rows of the key, applied in order.
    CHECK(converted(items, "(items glider rope)") ==
          "((items #drop climbing_gloves lamp_oil spyglass) (items #append rope))");
    // A value none of whose base survives is restated, pinned to what it replaced.
    CHECK(converted("(speed 3.5)", "(speed 4.5)") == "((speed 4.5 #was 3.5))");
    CHECK(converted("(spawn 20 0 12)", "(spawn 24 0 12)") == "((spawn 24 0 12 #was 20 0 12))");
    // Duel's agent: a list grown, a value pinned with its note, a row unchanged left out.
    CHECK(converted("(fighters (kestrel (moves (jab (cancels jab low_kick) (damage 30)))))\n"
                    "(cpu (easy (reaction 30) (block 0.2)))",
                    "(fighters (kestrel (moves (jab (cancels jab low_kick palm) (damage 30)))))\n"
                    "(cpu (easy (reaction 26 (note \"blocked too often\")) (block 0.2)))") ==
          "((fighters (kestrel (moves (jab (cancels #append palm))))) "
          "(cpu (easy (reaction 26 #was 30 (note \"blocked too often\")))))");
}

TEST_CASE("Hearth's things: a new kind is a keyed child, a repeated kind needs the verbs") {
    const char* cottage =
        "(households (cottage (things (fridge -6.3 -4.2 0) (bed 4.5 3.6 180) (phone -2 4.6 180))))";
    // A new kind of thing composes by key today: the converter keeps the keyed form.
    CHECK(converted(cottage,
                    "(households (cottage (things (fridge -6.3 -4.2 0) (bed 4.5 3.6 180) "
                    "(phone -2 4.6 180) (lamp -2 4 0))))") ==
          "((households (cottage (things (lamp -2 4 0)))))");
    // Taking one out is a #drop of the tuple as written; today the restated list keeps it.
    const char* without_phone =
        "(households (cottage (things (fridge -6.3 -4.2 0) (bed 4.5 3.6 180))))";
    CHECK(converted(cottage, without_phone, false) ==
          "((households (cottage (things #drop (phone -2 4.6 180)))))");
    CHECK(to_string(*merge(*parse(cottage), *parse(without_phone), MergePolicy::Overwrite)) ==
          "((households (cottage (things (fridge -6.3 -4.2 0) (bed 4.5 3.6 180) "
          "(phone -2 4.6 180)))))");
    // A second bed: restated or keyed, it retunes the first, so the verb appends it.
    const char* two_beds =
        "(households (cottage (things (fridge -6.3 -4.2 0) (bed 4.5 3.6 180) "
        "(phone -2 4.6 180) (bed 1 1 0))))";
    CHECK(converted(cottage, two_beds, false) ==
          "((households (cottage (things #append (bed 1 1 0)))))");
    CHECK(to_string(*merge(*parse(cottage), *parse(two_beds), MergePolicy::Overwrite)) ==
          "((households (cottage (things (fridge -6.3 -4.2 0) (bed 1 1 0) "
          "(phone -2 4.6 180)))))");
}

TEST_CASE("the stacks' lists: what a package writes today and with a verb") {
    size_t lists = 0, values = 0;
    size_t restated = 0, appended = 0;
    for (const std::string& file : corpus_files()) {
        if (file.rfind("stacks/", 0) != 0 || file.find("/data/") == std::string::npos) continue;
        const std::string path = std::string(repository_root()) + "/" + file;
        auto root = read_dta_file(path);
        if (!root) continue;
        for (const syntax::ListRow& r : syntax::list_rows(**root)) {
            if (r.file != path) continue;  // an included file's, counted there
            ++lists;
            INFO(file << ":" << r.line << " " << r.path);
            if (r.kind == syntax::ListKind::OneValue) {
                CHECK(r.add_today_works);  // restated whole, as today; no verb reaches inside
                continue;
            }
            CHECK(r.add_same);
            CHECK(r.drop_same);
            if (r.kind == syntax::ListKind::Values) {
                ++values;
                CHECK(r.add_today_works);  // restating works for values, at the list's size
                restated += r.add_before;
                appended += r.add_after;
                // Restating cannot empty a list: a row with no values replaces nothing.
                CHECK(r.drop_today_works == (r.elements > 1));
            }
            if (r.kind == syntax::ListKind::KeyedRepeats) {
                CHECK_FALSE(r.add_today_works);  // a keyed child retunes the first of a kind
                CHECK_FALSE(r.drop_today_works);
            }
        }
    }
    CHECK(lists == 26);
    CHECK(values == 15);
    CHECK(appended < restated);
}

TEST_CASE("a misplaced verb is a located error") {
    auto r = syntax::read("(cpu\n   (easy (reaction #was 30)))\n", "agent.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "agent.dta");
    CHECK(r.error().line() == 2);
    CHECK(r.error().code() == ErrorCode::Directive);
    CHECK(std::string(r.error().message()).find("(key v... #was w...)") != std::string::npos);
}

TEST_CASE("the samples read over their stacks as their comments say") {
    const std::string root(repository_root());
    auto file = [&](const std::string& rel) {
        auto r = read_dta_file(root + "/" + rel);
        REQUIRE(r);
        return *r;
    };
    auto value = [](const Ref<Array>& tree, const char* path) {
        const Array* a = tree->try_get(path);
        return a ? to_string(*a) : std::string("<none>");
    };
    const std::string samples = "experiments/syntax-layer-markers/samples/";
    // Wayfarer: the two DLCs compose with the verbs; restated, the second loses the rope.
    const Ref<Array> wayfarer = file("stacks/wayfarer/data/default.dta");
    auto stack = [&](const Ref<Array>& base, const char* a, const char* b) {
        return merge(*merge(*base, *file(samples + a), MergePolicy::Overwrite), *file(samples + b),
                     MergePolicy::Overwrite);
    };
    CHECK(value(stack(wayfarer, "wayfarer/dlc_a.dta", "wayfarer/dlc_b.dta"), "items") ==
          "(items climbing_gloves glider lamp_oil spyglass rope torch)");
    CHECK(value(stack(wayfarer, "wayfarer/dlc_a.today.dta", "wayfarer/dlc_b.today.dta"), "items") ==
          "(items climbing_gloves glider lamp_oil spyglass torch)");
    // Hearth: the phone out, the lamp in, a third bed.
    const Ref<Array> hearth =
        merge(*file("stacks/hearth/data/default.dta"), *file(samples + "hearth/cottage_dlc.dta"),
              MergePolicy::Overwrite);
    CHECK(value(hearth, "households.cottage.things") ==
          "(things (fridge -6.3 -4.2 0) (stove -5.3 -4.2 0) (table -4 -1 0) (shower -6.2 4.2 180) "
          "(sofa 3.5 -4.2 0) (tv 3.5 -1 180) (bed 4.5 3.6 180) (books 6.5 1 -90) (lamp -2 4 0))");
    size_t beds = 0;
    for (const Node& n : *hearth->try_get("households.farmhouse.things"))
        beds += n.type() == Type::Array && n.array()->tag() == Symbol("bed");
    CHECK(beds == 3);
    // Duel: palm after the jab's cancels; the pin and the notes leave the values as the
    // today form's.
    const Ref<Array> duel = merge(*file("stacks/duel/data/default.dta"),
                                  *file(samples + "duel/agent.dta"), MergePolicy::Overwrite);
    CHECK(value(duel, "fighters.kestrel.moves.jab.cancels") == "(cancels jab low_kick palm)");
    CHECK(value(duel, "cpu.easy.reaction") ==
          "(reaction 26 (note \"easy blocked the first low kick too often\"))");
    CHECK(syntax::marker_free(*duel));
}
