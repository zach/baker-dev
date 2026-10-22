// Layer verbs beside #remove: #append, #drop, and the #was pin
// (dev/syntax-layer-markers-design.md).
#include <doctest.h>
#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include "temp_dir.hpp"

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

}  // namespace

TEST_CASE("the verbs and the pin read in their slots and print back bare") {
    for (const char* text : {"(items #append rope torch)", "(things #drop (phone -2 4.6 180))",
                             "(reaction 26 #was 30)", "(reaction 26 #was 30 (note \"why\"))",
                             "(menu (debug_menu #remove))", "(debug_menu #remove #was 1)",
                             "(pos 1 2 #was 3 4)", "(reaction 26 (note \"why\") #was 30)"}) {
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
        {"(a 1 2 #was 3)", "(key v... #was w...)"},
        {"(a 1 #was 2 3)", "(key v... #was w...)"},
        {"(a #append 1 #was 2)", "(key v... #was w...)"},
        {"(a (b 1) #was 2)", "(key v... #was w...)"},
        {"(a 1 #was 2 #was 3)", "(key v... #was w...)"},
        {"(a #remove #was)", "(key #remove)"},
        {"(a #remove 1 #was 1)", "(key #remove)"},
        {"(a #remove (note \"x\"))", "(key #remove)"},
    };
    for (const Case& c : cases) {
        auto r = read_string(c.text, "bad.dta");
        REQUIRE_FALSE(r);
        CHECK(r.error().code() == ErrorCode::Directive);
        CHECK(r.error().line() == 1);
        INFO(c.text << ": " << r.error().message());
        CHECK(std::string(r.error().message()).find(c.hint) != std::string::npos);
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
    // Deep inside a new key too.
    CHECK(merged("(x 1)", "(k (a (b #append 1) (c #drop 2) (d #remove) (e 5 #was 6)))") ==
          "((x 1) (k (a (b 1) (e 5))))");
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
