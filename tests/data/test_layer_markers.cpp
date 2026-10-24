// Layer verbs beside #remove: #append, #drop, and the #was pin
// (dev/syntax-layer-markers-design.md).
#include <doctest.h>
#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include "temp_dir.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "ok.hpp"

using namespace baker;
using baker::test::ok;
using baker::test::TempDir;

namespace {

Ref<Array> parse(const char* text) {
    return ok(read_string(text, "layer.dta"));
}

std::string merged(const char* base, const char* over) {
    return to_string(*merge(*parse(base), *parse(over)));
}

const LayerEffectEntry* entry(const List<LayerEffectEntry>& effect, const char* path) {
    for (const LayerEffectEntry& e : effect)
        if (e.path == Symbol(path)) return &e;
    return nullptr;
}

bool has(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

const char* const kCancels = "fighters.kestrel.moves.jab.cancels";

}  // namespace

TEST_CASE("the verbs and the pin read in their slots and print back bare") {
    for (const char* text : {"(items #append rope torch)", "(things #drop (phone -2 4.6 180))",
                             "(reaction 26 #was 30)", "(reaction 26 #was 30 (note \"why\"))",
                             "(menu (debug_menu #remove))", "(debug_menu #remove #was 1)",
                             "(pos 1 2 #was 3 4)", "(reaction 26 (note \"why\") #was 30)",
                             // #was pins whatever the stack held: a list that grew or shrank too.
                             "(cancels jab low_kick palm #was jab low_kick)", "(a 1 #was 2 3)"}) {
        auto tree = parse(text);
        CHECK(tree->has_marker());
        CHECK(to_string(*tree) == "(" + std::string(text) + ")");
    }
    // Elsewhere the symbol is quoted, so it reads back as the symbol it is.
    for (const char* text : {"('#append')", "(a '#was' 1)", "(a 1 '#was')", "{f '#drop'}",
                             "(a 1 '#was' 2 '#was' 3)", "(a (b '#drop' 1))"}) {
        auto tree = ok(read_string(text, "q.dta"));
        const std::string printed = to_dta(*tree);
        CHECK(printed.find('\'') != std::string::npos);
        CHECK(structurally_equal(*tree, *ok(read_string(printed, "back.dta"))));
    }
    // A quoted marker in a slot the parser takes it in is the same symbol in the same place:
    // it prints bare and reads back as the same tree.
    auto quoted = ok(read_string("(a '#append' (b #remove))", "q.dta"));
    CHECK(to_dta(*quoted) == "(a #append (b #remove))\n");
    CHECK(structurally_equal(*quoted, *ok(read_string(to_dta(*quoted), "back.dta"))));
    // An array built in C++ carries the flag too, and prints as the parser reads it.
    auto root_of = [](std::vector<Node> run) {
        std::vector<Node> outer{Node(Array::make_from(run))};
        return Array::make_from(outer);
    };
    auto built = root_of({Node(Symbol("items")), Node(Symbol("#append")), Node(Symbol("rope"))});
    CHECK(built->has_marker());
    CHECK(to_string(*built) == "((items #append rope))");
    CHECK(to_string(*root_of({Node(Symbol("items")), Node(1), Node(Symbol("#append"))})) ==
          "((items 1 '#append'))");
    // Data without markers carries no flag, so the merge and the layer tools pay one test.
    CHECK_FALSE(parse("(items a b) (x (y 1 2))")->has_marker());
}

TEST_CASE("every misplaced marker is a located Directive error with its form as the hint") {
    struct Case {
        const char* text;
        const char* hint;
    };
    const Case cases[] = {
        {"(a #append)", "(key #append v...)"},
        {"(a 1 #append 2)", "(key #append v...)"},
        {"(#append 1)", "(key #append v...)"},
        {"{f #append 1}", "(key #append v...)"},
        {"#drop", "(key #drop v...)"},
        {"(a #drop)", "(key #drop v...)"},
        {"(a #was 1)", "(key v... #was w...)"},
        {"(a 1 #was)", "(key v... #was w...)"},
        {"(a 1 #was (note \"x\"))", "(key v... #was w...)"},
        {"(a #append 1 #was 2)", "(key v... #was w...)"},
        {"(a (b 1) #was 2)", "(key v... #was w...)"},
        {"(a 1 #was 2 #was 3)", "(key v... #was w...)"},
        {"(a #remove #was)", "(key #remove)"},
        {"(a #remove 1 #was 1)", "(key #remove)"},
        {"(a #remove (note \"x\"))", "(key #remove)"},
        {"[p #was 1]", "(key v... #was w...)"},
    };
    for (const Case& c : cases) {
        auto r = read_string(c.text, "bad.dta");
        INFO(c.text);
        REQUIRE_FALSE(r);
        CHECK(r.error().code() == ErrorCode::Directive);
        CHECK(r.error().line() == 1);
        INFO(r.error().message());
        CHECK(has(r.error().message(), c.hint));
    }
}

TEST_CASE("#append composes lists in stack order, and a key below is created from the operands") {
    // content-updates.md scenario 2: two DLCs extend one list, and both survive.
    CHECK(merged("(items climbing_gloves glider lamp_oil spyglass)", "(items #append rope)") ==
          "((items climbing_gloves glider lamp_oil spyglass rope))");
    CHECK(merged("(items a b rope)", "(items #append torch (sub 1))") ==
          "((items a b rope torch (sub 1)))");
    // A key the stack lacks: #append creates it with the operands, #drop adds nothing, and
    // nothing in the tree is a marker.
    CHECK(merged("(x 1)", "(items #append rope) (gone #drop rope) (y 2 #was 3)") ==
          "((x 1) (items rope) (y 2))");
    CHECK_FALSE(merge(*parse("(x 1)"), *parse("(items #append rope)"))->has_marker());
    // Deep inside a new key too, and inside an operand.
    CHECK(merged("(x 1)", "(k (a (b #append 1) (c #drop 2) (d #remove) (e 5 #was 6)))") ==
          "((x 1) (k (a (b 1) (e 5))))");
    const Ref<Array> op = merge(*parse("(items a)"), *parse("(items #append (t (u #remove)) z)"));
    CHECK(to_string(*op) == "((items a (t) z))");
    CHECK_FALSE(op->has_marker());
}

TEST_CASE("#drop removes the first structurally equal node, tuples restated as written") {
    CHECK(merged("(things (fridge -6.3 -4.2 0) (phone -2 4.6 180) (bed 1 2 3))",
                 "(things #drop (phone -2 4.6 180))") ==
          "((things (fridge -6.3 -4.2 0) (bed 1 2 3)))");
    // A tuple not present, or not restated byte for byte, drops nothing.
    CHECK(merged("(things (phone -2 4.6 180))", "(things #drop (phone -2 4.6 0) lamp)") ==
          "((things (phone -2 4.6 180)))");
    // Duplicate tuples: one per operand, the first each time.
    CHECK(merged("(things (bed 6 -4.6 0) (bed 6 4.6 180) (bed 6 -4.6 0))",
                 "(things #drop (bed 6 -4.6 0))") == "((things (bed 6 4.6 180) (bed 6 -4.6 0)))");
    CHECK(merged("(cancels jab low_kick palm)", "(cancels #drop low_kick jab)") ==
          "((cancels palm))");
    // Where a plain row would merge into the first of two same-tagged tuples, #append adds a
    // third: Hearth's farmhouse has two beds.
    CHECK(merged("(things (bed 6 -4.6 0) (bed 6 4.6 180))", "(things (bed 1 1 0))") ==
          "((things (bed 1 1 0) (bed 6 4.6 180)))");
    CHECK(merged("(things (bed 6 -4.6 0) (bed 6 4.6 180))", "(things #append (bed 1 1 0))") ==
          "((things (bed 6 -4.6 0) (bed 6 4.6 180) (bed 1 1 0)))");
}

TEST_CASE("#was is stripped by the merge and ignored by #merge, which skips the verbs") {
    CHECK(merged("(cpu (easy (reaction 30) (block 0.2)))",
                 "(cpu (easy (reaction 26 #was 30 (note \"blocked too often\"))))") ==
          "((cpu (easy (reaction 26 (note \"blocked too often\")) (block 0.2))))");
    CHECK(merged("(menu (debug_menu 1) (x 2))", "(menu (debug_menu #remove #was 1))") ==
          "((menu (x 2)))");
    TempDir dir;
    dir.write("over.dta", "(a (b #append 2) (c #drop 1) (d #remove) (e 5 #was 6) (f 7))");
    const std::string main = dir.write("main.dta", "(a (b 1) (c 1))\n#merge over.dta\n");
    CHECK(to_string(*ok(read_file(main))) == "((a (b 1) (c 1) (e 5 #was 6) (f 7)))");
}

TEST_CASE("a layer read from a cache keeps the flag and still composes") {
    auto layer = parse("(items #append rope)\n(cpu (easy (reaction 26 #was 30)))");
    REQUIRE(layer->has_marker());
    std::string bytes;
    REQUIRE(write_cache(*layer, string_out(bytes)));
    auto cached = ok(read_cache_bytes(bytes, "layer.datb"));
    CHECK(cached->has_marker());
    CHECK(to_string(*merge(*parse("(items a)\n(cpu (easy (reaction 30)))"), *cached)) ==
          "((items a rope) (cpu (easy (reaction 26))))");
}

TEST_CASE("two packages' #append compose in stack order, placement included (scenario 2, D144)") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer(
        "base", dir.write("base.dta", "(items climbing_gloves glider lamp_oil spyglass)\n")));
    REQUIRE(cfg.push_layer("dlc_a", dir.write("dlc_a.dta", "(items #append rope)\n")));
    REQUIRE(cfg.push_layer("dlc_b", dir.write("dlc_b.dta", "(items #append torch)\n")));
    CHECK(to_string(*cfg.root()) ==
          "((items climbing_gloves glider lamp_oil spyglass rope torch))");
    // Each package's effect is its verb and operands over the list below it.
    const auto effect = ok(cfg.layer_effect("dlc_b"));
    REQUIRE(effect.size() == 1);
    CHECK(effect[0].op == Symbol("#append"));
    CHECK(to_string(effect[0].value) == "torch");
    CHECK(to_string(*effect[0].below) == "(climbing_gloves glider lamp_oil spyglass rope)");
    CHECK_FALSE(effect[0].shadowed);
    // The two differ by operand; the same verb with the same operands is no difference.
    const auto d = ok(cfg.diff("dlc_a", "dlc_b"));
    REQUIRE(d.size() == 1);
    CHECK((d[0].op_a == Symbol("#append") && d[0].op_b == Symbol("#append")));
    // Removing a package takes its operand out; placing one below another changes the order.
    REQUIRE(cfg.remove_layer("dlc_a"));
    CHECK(to_string(*cfg.root()) == "((items climbing_gloves glider lamp_oil spyglass torch))");
    REQUIRE(cfg.push_layer("dlc_a", dir.path("dlc_a.dta"), {.below = "dlc_b"}));
    CHECK(to_string(*cfg.root()) ==
          "((items climbing_gloves glider lamp_oil spyglass rope torch))");
    REQUIRE(cfg.push_layer("patch", dir.write("patch.dta", "(items #drop glider rope)\n"),
                           {.below = "dlc_a"}));
    CHECK(to_string(*cfg.root()) == "((items climbing_gloves lamp_oil spyglass rope torch))");
    // A restated list above them all still replaces the whole list.
    REQUIRE(cfg.push_layer("fix", dir.write("fix.dta", "(items lamp_oil)\n")));
    CHECK(to_string(*cfg.root()) == "((items lamp_oil))");
}

TEST_CASE("source() names the literal run's layer, explain shows Composes, write-back refuses") {
    TempDir dir;
    const std::string base_text =
        "(fighters (kestrel (moves (jab (cancels jab low_kick) (damage 30)))))\n";
    const std::string agent_text =
        "(fighters (kestrel (moves (jab (cancels #append palm) (damage 32)))))\n";
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default", dir.write("default.dta", base_text)));
    REQUIRE(cfg.push_layer("agent", dir.write("agent.dta", agent_text)));
    CHECK(to_string(ok(cfg.get<Node>(kCancels))) == "jab");
    CHECK(to_string(*cfg.root()->try_get(kCancels)) == "(cancels jab low_kick palm)");

    // The tools judge's hazard: the agent's row has values, but supplies none.
    const Source s = ok(cfg.source(kCancels));
    CHECK(s.layer == Symbol("default"));
    CHECK(s.line == 1);
    CHECK(ok(cfg.source("fighters.kestrel.moves.jab.damage")).layer == Symbol("agent"));

    const auto entries = ok(cfg.explain(kCancels));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].layer == Symbol("default"));
    CHECK(entries[0].standing == Standing::Wins);
    CHECK(entries[1].layer == Symbol("agent"));
    CHECK(entries[1].standing == Standing::Composes);
    CHECK(entries[1].op == Symbol("#append"));
    CHECK(to_string(entries[1].value) == "palm");
    CHECK(has(entries[1].source.file.str(), "agent.dta"));
    CHECK(entries[1].source.line == 1);

    if constexpr (!kShipping) {
        // Writing the composed list anywhere at or below the verb would append palm twice.
        const std::vector<Node> live{Node(Symbol("jab")), Node(Symbol("palm"))};
        REQUIRE(cfg.set(kCancels, std::span<const Node>(live)));
        for (const auto& r : {cfg.write_back(kCancels), cfg.write_back_to(kCancels, "default"),
                              cfg.write_back_to(kCancels, "agent")}) {
            REQUIRE_FALSE(r);
            CHECK(r.error().code() == ErrorCode::Refused);
            INFO(r.error().message());
            CHECK(has(r.error().message(),
                      "'fighters/kestrel/moves/jab/cancels' is composed by layer 'agent': "
                      "(cancels #append palm); write the base or promote the layer"));
            CHECK(has(r.error().file().str(), "agent.dta"));
            CHECK(r.error().line() == 1);
        }
        CHECK(dir.read("default.dta") == base_text);
        CHECK(dir.read("agent.dta") == agent_text);
        // A layer above the verb restates the list, which is what it reads.
        REQUIRE(cfg.push_layer("top", dir.path("top.dta"), {.optional = true}));
        REQUIRE(cfg.write_back_to(kCancels, "top"));
        CHECK(dir.read("top.dta") == "(fighters (kestrel (moves (jab (cancels jab palm)))))\n");
        // The agent's plain rows write back as before.
        REQUIRE(cfg.set("fighters.kestrel.moves.jab.damage", Node(int64_t{33})));
        REQUIRE(cfg.write_back("fighters.kestrel.moves.jab.damage"));
        CHECK(has(dir.read("agent.dta"), "(cancels #append palm) (damage 33)"));
    }
}

TEST_CASE("a pinned row: fresh passes, a moved base warns at the row, warnings_refuse refuses") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer(
        "default",
        dir.write("default.dta", "(cpu (easy (reaction 30) (block 0.2)))\n(menu (debug 1))\n")));
    const char* agent =
        "(cpu (easy (reaction 26 #was 30 (note \"easy blocked the first low kick too often\"))))\n"
        "(menu (debug #remove #was 1))\n";
    const LayerReport fresh = ok(cfg.check_layer_text("agent", agent));
    CHECK(fresh.warnings.empty());
    CHECK(fresh.ok());
    for (const LayerEffectEntry& e : fresh.effect) CHECK_FALSE(e.stale);

    // The base moves to 32 and the debug menu to 2.
    Config moved({.tweaks = nullptr});
    REQUIRE(moved.push_layer("default", dir.write("moved.dta",
                                                  "(cpu (easy (reaction 32) "
                                                  "(block 0.2)))\n(menu (debug 2))\n")));
    const LayerReport stale = ok(moved.check_layer_text("agent", agent));
    REQUIRE(stale.warnings.size() == 2);
    CHECK(std::string(stale.warnings[0].message()) ==
          "'cpu.easy.reaction' was 30 when this row was written; the stack has 32");
    CHECK(stale.warnings[0].file() == Symbol("agent"));
    CHECK(stale.warnings[0].line() == 1);
    CHECK(std::string(stale.warnings[1].message()) ==
          "'menu.debug' was 1 when this row was written; the stack has 2");
    CHECK(stale.warnings[1].line() == 2);
    CHECK(stale.ok());  // a warning, unless the policy says warnings refuse
    REQUIRE(entry(stale.effect, "cpu.easy.reaction"));
    CHECK(entry(stale.effect, "cpu.easy.reaction")->stale);
    CHECK(to_string(entry(stale.effect, "cpu.easy.reaction")->value) == "26");
    CHECK(entry(stale.effect, "menu.debug")->op == Symbol("#remove"));

    const LayerPolicy refuse{.warnings_refuse = true};
    CHECK_FALSE(ok(moved.check_layer_text("agent", agent, refuse)).ok());
    CHECK(ok(cfg.check_layer_text("agent", agent, refuse)).ok());
    const std::string before = to_string(*moved.root());
    CHECK_FALSE(moved.push_layer_text("agent", agent, {.policy = &refuse}));
    CHECK(to_string(*moved.root()) == before);
    CHECK(strings(moved.layers()) == std::vector<std::string>{"default", "scratch"});

    // A key the stack lacks is stale too: its pin names a value no longer there.
    const LayerReport gone = ok(cfg.check_layer_text("x", "(cpu (hard (reaction 9 #was 10)))"));
    REQUIRE(gone.warnings.size() == 1);
    CHECK(has(gone.warnings[0].message(), "was 10 when this row was written; the stack has none"));

    // A plain push of a file layer sends the same warnings to the handler, and applies.
    std::vector<std::string> warned;
    moved.set_warning_handler([&](const Error& e) { warned.emplace_back(e.message()); });
    REQUIRE(moved.push_layer("agent", dir.write("agent.dta", agent)));
    REQUIRE(warned.size() == 2);
    CHECK(warned[0] == "'cpu.easy.reaction' was 30 when this row was written; the stack has 32");
    CHECK(moved.get<int>("cpu.easy.reaction") == 26);
    CHECK_FALSE(moved.get<int>("menu.debug"));
    CHECK(entry(ok(moved.layer_effect("agent")), "cpu.easy.reaction")->stale);
}

#if !BAKER_SHIPPING
TEST_CASE("write_back keeps a row's pin; write_back_to with a pin writes one") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("default",
                           dir.write("default.dta", "(cpu (easy (reaction 30) (block 0.2)))\n")));
    REQUIRE(
        cfg.push_layer("agent", dir.write("agent.dta", "(cpu (easy (reaction 26 #was 30)))\n")));
    REQUIRE(cfg.set("cpu.easy.reaction", Node(int64_t{24})));
    REQUIRE(cfg.write_back("cpu.easy.reaction"));
    CHECK(dir.read("agent.dta") == "(cpu (easy (reaction 24 #was 30)))\n");
    // A new row, pinned to the value below the layer.
    REQUIRE(cfg.set("cpu.easy.block", Node(0.35)));
    REQUIRE(cfg.write_back_to("cpu.easy.block", "agent", true));
    CHECK(dir.read("agent.dta") == "(cpu (easy (reaction 24 #was 30) (block 0.35 #was 0.2)))\n");
    CHECK(cfg.get<double>("cpu.easy.block") == 0.35);
    // An existing row without a pin gains one after its values; a list pins whole.
    Config list({.tweaks = nullptr});
    REQUIRE(list.push_layer("default", dir.write("l0.dta", "(cancels jab low_kick)\n")));
    REQUIRE(list.push_layer("agent", dir.write("l1.dta", "(cancels jab)\n")));
    const std::vector<Node> three{Node(Symbol("jab")), Node(Symbol("low_kick")),
                                  Node(Symbol("palm"))};
    REQUIRE(list.set("cancels", std::span<const Node>(three)));
    REQUIRE(list.write_back_to("cancels", "agent", true));
    CHECK(dir.read("l1.dta") == "(cancels jab low_kick palm #was jab low_kick)\n");
    REQUIRE(list.remove_layer("agent"));
    CHECK(ok(list.check_layer_text("again", dir.read("l1.dta"))).warnings.empty());
}

TEST_CASE("promote deletes a removed key's text and writes composed lists resolved (scenario 5)") {
    TempDir dir;
    const std::string base_text =
        "(menu\n"
        "   (debug_menu 1)   ; for the team\n"
        "   (items play options credits))\n"
        "(enemies (grunt (hp 10)) (exploit_boss (hp 1)))\n"
        "(cheats\n"
        "   (god 1))\n";
    Config cfg({.tweaks = nullptr});
    REQUIRE(cfg.push_layer("base", dir.write("base.dta", base_text)));
    REQUIRE(cfg.push_layer("patch", dir.write("patch.dta",
                                              "(menu (debug_menu #remove) (items #drop options))\n"
                                              "(enemies (exploit_boss #remove))\n"
                                              "(cheats #remove)\n(missing #remove)\n")));
    const std::string merged_before = to_string(*cfg.root());
    CHECK(merged_before == "((menu (items play credits)) (enemies (grunt (hp 10))))");
    const WriteReport r = ok(cfg.promote("patch", "base"));
    CHECK(r.ok());
    CHECK(dir.read("base.dta") ==
          "(menu\n"
          "   (items play credits))\n"
          "(enemies (grunt (hp 10)))\n");
    // The stack reads the same once the patch is emptied.
    REQUIRE(cfg.remove_layer("patch"));
    CHECK(to_string(*cfg.root()) == merged_before);

    // A row a layer above the target composes again is refused, and nothing is written.
    REQUIRE(cfg.push_layer("dlc", dir.write("dlc.dta", "(menu (items #append bonus))\n")));
    REQUIRE(cfg.set("menu.items", Node(Symbol("play"))));
    const WriteReport refused = ok(cfg.promote("scratch", "base"));
    REQUIRE(refused.refused.size() == 1);
    CHECK(has(refused.refused[0].message(), "is composed by layer 'dlc'"));
    CHECK(dir.read("base.dta") == "(menu\n   (items play credits))\n(enemies (grunt (hp 10)))\n");
}
#endif

TEST_CASE("the policy reads a verb's operands; existing_only refuses #append of a new key") {
    TempDir dir;
    Config cfg({.tweaks = nullptr});
    REQUIRE(
        cfg.push_layer("default", dir.write("default.dta",
                                            "(households (cottage (things (phone -2 4.6 180))))\n"
                                            "(items a b)\n")));
    const LayerPolicy scalars{.scalars_only = true};
    auto r = ok(cfg.check_layer_text("dlc", "(households (cottage (things #append (lamp -2 4 0))))",
                                     scalars));
    REQUIRE(r.refusals.size() == 1);
    CHECK(has(r.refusals[0].message(), "is (lamp -2 4 0), not a number"));
    CHECK(ok(cfg.check_layer_text("dlc", "(items #append c d)", scalars)).ok());

    const LayerPolicy existing{.existing_only = true};
    CHECK(ok(cfg.check_layer_text("dlc", "(items #append c)\n(items2 #drop a)", existing))
              .refusals.size() == 1);
    CHECK(ok(cfg.check_layer_text("dlc", "(items #drop a)", existing)).ok());
    CHECK_FALSE(ok(cfg.check_layer_text("dlc", "(items #remove)", existing)).ok());
    CHECK(ok(cfg.check_layer_text("dlc", "(items a b c #was a b)", existing)).ok());
}

TEST_CASE("no reader of a layer's own tree takes its values raw: leaf_values is the one way") {
    const std::string root = std::string(BAKER_CORPUS_DIR) + "/../../src/data/";
    auto read = [&](const char* name) {
        std::ifstream in(root + name);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    for (const char* name :
         {"config.cpp", "config_check.cpp", "config_write.cpp", "config_impl.hpp"}) {
        const std::string src = read(name);
        INFO(name);
        REQUIRE_FALSE(src.empty());
        CHECK_FALSE(has(src, "values_of("));
    }
    // collect_leaves, value_nodes (explain, write_back's check of scratch), promote's leaves,
    // and the effect, diff and check all reach a layer row through it.
    CHECK(has(read("config_write.cpp"), "out.push_back({path, &child, leaf_values(child)})"));
    CHECK(has(read("config_write.cpp"), "leaf.values = leaf_values(*leaf.holder)"));
    CHECK(has(read("config.cpp"), "out = leaf_values(*r.array)"));
}
