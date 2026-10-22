// The experiment's tests: the corpus round trip, one case per rule of the grammar, every
// error the reader reports, the two examples the idea was first rejected over, the
// printer's heuristic, and the samples against the files they were made from.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/std_interop.hpp>

#include <doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace baker;
using namespace baker::experiments;

namespace {

// Both texts read to the same tree; the first difference otherwise.
std::string same_tree(std::string_view headed, std::string_view nested) {
    auto a = syntax::read(headed, "headed.dta");
    if (!a) return "headed: " + std::string(to_string(a.error()));
    auto b = read_string(nested, "nested.dta");
    if (!b) return "nested: " + std::string(to_string(b.error()));
    return first_difference(**a, **b);
}

// The reader's error for `text`: its message, line and column as "message @line:column".
std::string error_of(std::string_view text) {
    auto r = syntax::read(text, "t.dta");
    if (r) return "no error";
    return std::string(r.error().message()) + " @" + std::to_string(r.error().line()) + ":" +
           std::to_string(r.error().column());
}

// A copy with every array's tagged children in tag order, for comparing trees that differ
// only in the order of a record's fields.
Ref<Array> canonical(const Array& a) {
    std::vector<Node> nodes;
    for (const Node& n : a) {
        if (n.type() == Type::Array)
            nodes.emplace_back(canonical(*n.array()));
        else
            nodes.push_back(n);
    }
    if (!nodes.empty()) {
        auto key = [](const Node& n) {
            const Array* arr = n.try_array();
            return arr ? std::string(arr->tag().str()) : std::string();
        };
        std::stable_sort(nodes.begin() + 1, nodes.end(),
                         [&](const Node& x, const Node& y) { return key(x) < key(y); });
    }
    return Array::make_from(nodes, a.file(), a.line());
}

const char* kMovesNested =
    "(moves\n"
    "   (jab       (button light) (startup 4) (active 2) (recovery 7) (damage 30)\n"
    "              (hitstun 12) (blockstun 9) (push 0.12) (cancels jab low_kick)\n"
    "              (hitbox 0.3 1.3 0.55 0.2))\n"
    "   (low_kick  (button kick) (crouching 1) (startup 6) (active 3) (recovery 12)\n"
    "              (damage 45) (hitstun 16) (blockstun 10) (push 0.15) (guard low)\n"
    "              (cancels palm) (hitbox 0.3 0.1 0.6 0.25))\n"
    "   (side_kick (button kick) (startup 7) (active 3) (recovery 14) (damage 60)\n"
    "              (hitstun 17) (blockstun 11) (push 0.2) (hitbox 0.35 0.9 0.75 0.25)))\n";

}  // namespace

TEST_CASE("every corpus file survives the round trip") {
    const auto failures =
        round_trip_corpus([](const Array& r) { return syntax::print(r); }, syntax::reader());
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

// --- The grammar, one case per rule ---

TEST_CASE("atom cells become one tagged child each, on the row's line") {
    const char* headed =
        "(goods\n"
        "   #columns (base_price demand spoil_days volatility)\n"
        "   (wheat    2          300    0          0.1)    ; Goods, row 2\n"
        "   (flour    10         120    60         0.1))\n";
    CHECK(same_tree(headed,
                    "(goods (wheat (base_price 2) (demand 300) (spoil_days 0) (volatility 0.1))\n"
                    "       (flour (base_price 10) (demand 120) (spoil_days 60) (volatility 0.1)))")
              .empty());
    auto root = syntax::read(headed, "goods.dta");
    REQUIRE(root);
    const Array* flour = (*root)->try_get("goods.flour");
    REQUIRE(flour);
    CHECK(flour->line() == 4);
    REQUIRE(flour->try_find("demand"));
    CHECK(flour->try_find("demand")->line() == 4);
    CHECK(flour->try_find("demand")->file().str() == "goods.dta");
    REQUIRE((*root)->try_get("goods.wheat.volatility"));
    CHECK((*root)->try_get("goods.wheat.volatility")->line() == 3);
}

TEST_CASE("the frame-data example: an array cell with a symbol first, trailing children") {
    // The example the idea was rejected over: (jab low_kick) is lexically a tagged array,
    // and under the fixed-count rule it is the cancels cell all the same.
    const char* headed =
        "(moves\n"
        "   #columns (button startup active recovery damage hitstun blockstun push cancels "
        "hitbox)\n"
        "   (jab light 4 2 7 30 12 9 0.12 (jab low_kick) (0.3 1.3 0.55 0.2) (crouching 1)))\n";
    CHECK(same_tree(headed,
                    "(moves (jab (button light) (startup 4) (active 2) (recovery 7) (damage 30)\n"
                    "            (hitstun 12) (blockstun 9) (push 0.12) (cancels jab low_kick)\n"
                    "            (hitbox 0.3 1.3 0.55 0.2) (crouching 1)))")
              .empty());
}

TEST_CASE("the economy example: an array cell with -> first, a command cell") {
    const char* headed =
        "(buildings\n"
        "   #columns (cost upkeep rate recipe unlock)\n"
        "   (mill 1500 15 10 (wheat wheat -> flour) {>= {econ.total_earned} 1500})\n"
        "   (farm 400 6 20 (-> wheat) _))\n";
    CHECK(same_tree(headed,
                    "(buildings\n"
                    "   (mill (cost 1500) (upkeep 15) (rate 10) (recipe wheat wheat -> flour)\n"
                    "         (unlock {>= {econ.total_earned} 1500}))\n"
                    "   (farm (cost 400) (upkeep 6) (rate 20) (recipe -> wheat)))")
              .empty());
}

TEST_CASE("a hole adds no child; a property, variable, string and quoted cell") {
    CHECK(same_tree("(t #columns (a b c d e) (k _ [pos x] $speed \"two words\" 'odd name'))",
                    "(t (k (b [pos x]) (c $speed) (d \"two words\") (e 'odd name')))")
              .empty());
    CHECK(same_tree("(t #columns (a) (k _) (j -0x1F))", "(t (k) (j (a -31)))").empty());
}

TEST_CASE("an empty array cell is the column with no values; a nested list stays a list") {
    CHECK(same_tree("(t #columns (a b) (k () ((1 2) (3 4))))", "(t (k (a) (b (1 2) (3 4))))")
              .empty());
}

TEST_CASE("fewer cells than columns is a located error naming the row and the columns") {
    CHECK(error_of("(moves\n"
                   "   #columns (button startup active recovery damage hitstun blockstun push "
                   "hitbox)\n"
                   "   (jab light 4 2 7 30 12 9))\n") ==
          "row jab has 7 cells, #columns declares 9 (missing push hitbox) @3:4");
    CHECK(error_of("(t #columns (a b) (k 1))") ==
          "row k has 1 cell, #columns declares 2 (missing b) @1:19");
    CHECK(error_of("(t #columns (a b) (k))") ==
          "row k has 0 cells, #columns declares 2 (missing a b) @1:19");
}

TEST_CASE("a hole where the header has no column, and a hole as a key, are errors") {
    CHECK(error_of("(t #columns (a) (k 1 _))") ==
          "'_' after the 1 cells of row k: #columns has no column for it @1:22");
    CHECK(error_of("(t #columns (a) (_ 1))") == "a row's key cannot be '_' @1:18");
    // Outside a header, _ is an ordinary symbol.
    CHECK(same_tree("(t (k _ 1))", "(t (k _ 1))").empty());
    auto r = syntax::read("(t (k _))", "t.dta");
    REQUIRE(r);
    REQUIRE((*r)->try_get("t.k"));
    CHECK((*(*r)->try_get("t.k"))[1].is(Symbol("_")));
}

TEST_CASE("nodes after the cells are ordinary children, except one named like a column") {
    CHECK(same_tree("(t #columns (a) (k 1 (x 2) (y 3 4) 5 \"s\"))",
                    "(t (k (a 1) (x 2) (y 3 4) 5 \"s\"))")
              .empty());
    CHECK(error_of("(t #columns (a b) (k 1 _ (a 2)))") ==
          "row k writes a as a cell and as a child; write it once, as the cell @1:26");
}

TEST_CASE("a second header replaces the first for the rows after it") {
    CHECK(
        same_tree("(t #columns (a b) (k 1 2) #columns (c) (j 3))", "(t (k (a 1) (b 2)) (j (c 3)))")
            .empty());
}

TEST_CASE("children before the header are ordinary; a removal row passes through") {
    CHECK(same_tree("(t (k (a 1)) 5 #columns (a) (j 2))", "(t (k (a 1)) 5 (j (a 2)))").empty());
    CHECK(same_tree("(t #columns (a b) (k 1 2) (j #remove))", "(t (k (a 1) (b 2)) (j #remove))")
              .empty());
}

TEST_CASE("under a header every child is a row") {
    CHECK(error_of("(t #columns (a) 5)") ==
          "under #columns every child of t is a row, (key cells...), not 5 @1:17");
    CHECK(error_of("(t #columns (a) {f 1})") ==
          "under #columns every child of t is a row, (key cells...), not a command @1:17");
    CHECK(error_of("(t #columns (a) (1 2))") ==
          "a row under #columns starts with its key, a symbol, not 1 @1:17");
    CHECK(error_of("(t #columns (a) ((x) 2))") ==
          "a row under #columns starts with its key, a symbol, not an array @1:17");
    CHECK(error_of("(t #columns (a) ())") ==
          "a row under #columns starts with its key, a symbol @1:17");
    CHECK(error_of("(t #columns (a) (\"s\" 2))") ==
          "a row under #columns starts with its key, a symbol, not \"s\" @1:17");
}

TEST_CASE("the header's own errors") {
    CHECK(error_of("#columns (a)\n(t (k 1))") ==
          "#columns needs an enclosing parenthesized array: it declares the columns of that "
          "array's rows @1:1");
    CHECK(error_of("(t {f #columns (a) (k 1)})") ==
          "#columns is written inside a parenthesized array, not inside '{' @1:7");
    CHECK(error_of("(t #columns a (k 1))") ==
          "#columns takes a parenthesized list of column names on its line: #columns (name ...) "
          "@1:4");
    CHECK(error_of("(t #columns\n   (a) (k 1))") ==
          "#columns takes a parenthesized list of column names on its line: #columns (name ...) "
          "@1:4");
    CHECK(error_of("(t #columns (a 1) (k 1 2))") ==
          "#columns names each column with a symbol, not 1 @1:16");
    CHECK(error_of("(t #columns (a (b)) (k 1 2))") ==
          "#columns names each column with a symbol, not an array @1:16");
    CHECK(error_of("(t #columns (a a) (k 1 2))") == "#columns declares a twice @1:16");
    CHECK(error_of("(t #columns (a _) (k 1 2))") == "'_' is a hole, not a column name @1:16");
    CHECK(error_of("(t #columns () (k 1))") == "#columns declares no columns @1:4");
    CHECK(error_of("(t #columns (a\n(k 1)") == "the #columns list is not closed @1:4");
}

TEST_CASE("a directive is not a cell; between rows it passes through with its arguments") {
    CHECK(error_of("(t #columns (a b) (k #ifdef X 1 #endif 2))") ==
          "row k: #ifdef cannot be a cell (the cell for a) @1:22");
    CHECK(same_tree("#define X\n(t #columns (a) #ifdef X (k 1) #else (k 2) #endif (j 3))",
                    "(t (k 1) (j (a 3)))")
              .empty() == false);  // the headed k has (a 1), the nested k has 1
    CHECK(same_tree("#define X\n(t #columns (a) #ifdef X (k 1) #else (k 2) #endif (j 3))",
                    "(t (k (a 1)) (j (a 3)))")
              .empty());
    CHECK(same_tree("(t #columns (a) #define W (9) (k W))", "(t (k (a 9)))").empty());
}

TEST_CASE("a macro in a cell splices as one cell; a column name is never expanded") {
    CHECK(same_tree("[FAST 4] [BOX 0.3 1.3]\n(t #columns (startup hitbox) (k FAST BOX))",
                    "(t (k (startup 4) (hitbox 0.3 1.3)))")
              .empty());
    CHECK(same_tree("[startup 99]\n(t #columns (startup) (k 4))", "(t (k (startup 4)))").empty());
}

TEST_CASE("quoted column names and keys, and a table nested in a cell") {
    CHECK(
        same_tree("(t #columns ('odd name' b) ('a key' 1 2))", "(t ('a key' ('odd name' 1) (b 2)))")
            .empty());
    CHECK(same_tree("(t #columns (a) (k (#columns (x) (r 1))))", "(t (k (a (r (x 1)))))").empty());
}

TEST_CASE("the rewrite keeps every line and a trailing comment") {
    const std::string headed =
        "(goods\n"
        "   #columns (base_price demand)   ; the sheet's header\n"
        "   (wheat    2          300)      ; Goods, row 2\n"
        "   (flour    10         _))\n";
    auto out = syntax::desugar(headed, "g.dta");
    REQUIRE(out);
    CHECK(std::count(out->begin(), out->end(), '\n') ==
          std::count(headed.begin(), headed.end(), '\n'));
    CHECK(out->find("; Goods, row 2") != std::string::npos);
    CHECK(out->find("(wheat    (base_price 2)          (demand 300))") != std::string::npos);
    // Text without a header is untouched.
    auto same = syntax::desugar("(a (b 1))\n", "a.dta");
    REQUIRE(same);
    CHECK(*same == "(a (b 1))\n");
}

// --- The printer ---

TEST_CASE("the printer heads a keyed table whose rows agree on an order, and round-trips") {
    auto root = read_string(kMovesNested, "moves.dta");
    REQUIRE(root);
    const std::string printed = syntax::print(**root);
    INFO(printed);
    CHECK(printed.find("#columns (button crouching startup active recovery damage hitstun") !=
          std::string::npos);
    CHECK(printed.find("(jab") != std::string::npos);
    auto back = syntax::read(printed, "moves.new");
    REQUIRE(back);
    CHECK(first_difference(**root, **back).empty());
    const syntax::TableStats s = syntax::table_stats(**root);
    CHECK(s.candidates == 1);
    CHECK(s.tables == 1);
    CHECK(s.rows == 3);
    CHECK(s.columns == 12);
    CHECK(s.cells == 31);
    CHECK(s.holes == 5);
    CHECK(s.parens_removed == 2 * 27);  // four list cells (jab's cancels, each hitbox) keep theirs
}

TEST_CASE("the printer keeps nested text where a header would not read back the same tree") {
    auto nested = [](std::string_view text) {
        auto root = read_string(text, "t.dta");
        REQUIRE(root);
        const std::string printed = syntax::print(**root, {.width = 20});
        auto back = syntax::read(printed, "t.new");
        REQUIRE(back);
        CHECK(first_difference(**root, **back).empty());
        return printed.find("#columns") == std::string::npos;
    };
    // Two rows disagree on the order of a and b.
    CHECK(nested("(t (k (a 1) (b 2)) (j (b 3) (a 4)))"));
    // Coverage: 2 of 6 cells.
    CHECK(nested("(t (k (a 1)) (j (b 2)) (i (c 3)))"));
    // One row.
    CHECK(nested("(t (k (a 1) (b 2) (c 3) (d 4)))"));
    // A repeated field.
    CHECK(nested("(t (k (a 1) (a 2)) (j (a 3) (a 4)))"));
    // A field whose values hold an array.
    CHECK(nested("(t (k (a (x 1)) (b 2)) (j (a (x 3)) (b 4)))"));
    // A removal marker.
    CHECK(nested("(t (k (a #remove) (b 2)) (j (a 3) (b 4)))"));
    // A value that is the symbol _ is a table still, quoted in its cell.
    auto root = read_string("(t (k (a _) (b 2) (c 3)) (j (a 1) (b _) (c 3)))", "t.dta");
    REQUIRE(root);
    const std::string printed = syntax::print(**root, {.width = 20});
    CHECK(printed.find("'_'") != std::string::npos);
    auto back = syntax::read(printed, "t.new");
    REQUIRE(back);
    CHECK(first_difference(**root, **back).empty());
}

TEST_CASE("the printer aligns the header's names over the cells when the lines fit") {
    auto root = read_string(
        "(cpu (easy (reaction 30) (block 0.2)) (hard (reaction 12) (block 0.6)))", "t.dta");
    REQUIRE(root);
    CHECK(syntax::print(**root, {.width = 40}) ==
          "(cpu\n"
          "   #columns (reaction block)\n"
          "   (easy     30       0.2)\n"
          "   (hard     12       0.6))\n");
    CHECK(syntax::print(**root, {.width = 40, .align = false}) ==
          "(cpu\n"
          "   #columns (reaction block)\n"
          "   (easy 30 0.2)\n"
          "   (hard 12 0.6))\n");
    CHECK(syntax::print(**root, {.width = 40, .tables = false}) ==
          "(cpu\n"
          "   (easy (reaction 30) (block 0.2))\n"
          "   (hard (reaction 12) (block 0.6)))\n");
}

TEST_CASE("a table whose rows would not fit the width stays nested; a wide header wraps") {
    auto root = read_string(
        "(tracks\n"
        "   (canyon_jump (desc \"A run-up, a jump over the canyon, and a banked left to the "
        "line\")\n"
        "                (lay straight_40 kicker_15 gap_8 landing_20 left_90 straight_40) (par "
        "20))\n"
        "   (switchback (desc \"Bends to set up, then the jump\") (lay straight_20 left_45) (par "
        "25)))",
        "t.dta");
    REQUIRE(root);
    CHECK(syntax::print(**root).find("#columns") == std::string::npos);
    CHECK(syntax::print(**root, {.width = 200}).find("#columns") != std::string::npos);
    syntax::TableStats s = syntax::table_stats(**root);
    CHECK(s.rejected_width == 1);
    // Twelve names do not fit in 60 columns; the header wraps and reads back.
    auto moves = read_string(kMovesNested, "moves.dta");
    REQUIRE(moves);
    const std::string printed = syntax::print(**moves, {.width = 80});
    INFO(printed);
    CHECK(printed.find("#columns (button crouching startup active recovery damage hitstun "
                       "blockstun\n             push guard cancels hitbox)") != std::string::npos);
    auto back = syntax::read(printed, "moves.new");
    REQUIRE(back);
    CHECK(first_difference(**moves, **back).empty());
}

// --- The samples, against the files they were made from ---

TEST_CASE("each sample reads to its original's tree") {
    struct Sample {
        const char* sample;    // under experiments/syntax-column-tables/samples
        const char* original;  // under the repository root
        bool same_order;       // false: the sample writes some fields as trailing children
    };
    const Sample samples[] = {
        {"duel-default.columns.dta", "stacks/duel/data/default.dta", false},
        {"ledger-economy.columns.dta", "stacks/ledger/data/economy.dta", true},
        {"stuntline-default.columns.dta", "stacks/stuntline/data/default.dta", true},
        {"hearth-default.columns.dta", "stacks/hearth/data/default.dta", true},
    };
    const std::string root(repository_root());
    for (const Sample& s : samples) {
        INFO(s.sample);
        const std::string original_path = root + "/" + s.original;
        auto original = read_dta_file(original_path);
        REQUIRE(original);
        auto text = read_text_file(root + "/experiments/syntax-column-tables/samples/" + s.sample);
        if (!text) {
            FAIL_CHECK("missing sample " << s.sample);
            continue;
        }
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        session.add_include_dir(original_path.substr(0, original_path.rfind('/')));
        auto sample = syntax::read(*text, s.sample, session);
        REQUIRE(sample);
        if (s.same_order) {
            CHECK(first_difference(**original, **sample).empty());
        } else {
            CHECK(!first_difference(**original, **sample).empty());
            CHECK(first_difference(*canonical(**original), *canonical(**sample)).empty());
        }
    }
}
