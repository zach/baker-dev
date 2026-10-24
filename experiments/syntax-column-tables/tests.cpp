// The experiment's tests: the corpus round trip, one case per rule of the grammar, every
// error the reader reports, the two examples the idea was first rejected over, the
// printer's heuristic, and the samples against the files they were made from.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/std_interop.hpp>

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
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
std::string error_of(std::string_view text, const syntax::ReadOptions& options = {}) {
    auto r = syntax::read(text, "t.dta", options);
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

TEST_CASE("every corpus file survives the round trip under the strict rule too") {
    // The printer writes no children after a row's cells, so its text reads either way.
    const auto failures =
        round_trip_corpus([](const Array& r) { return syntax::print(r); },
                          [](std::string_view text, std::string_view name) {
                              return syntax::read(text, name, {.trailing_children = false});
                          });
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
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
    // Every row repeats a column's name, so one written across lines would move the lines
    // after each row. libFuzzer found it.
    CHECK(error_of("(t #columns ('a\nb' c)\n   (k 1 2))") ==
          "a column name is written on one line: every row repeats it @1:14");
}

TEST_CASE("the printer heads no table whose field tag spans lines") {
    auto root = read_string("(t (k ('a\nb' 1) (c 2)) (j ('a\nb' 3) (c 4)))", "t.dta");
    REQUIRE(root);
    const std::string printed = syntax::print(**root);
    CHECK(printed.find("#columns") == std::string::npos);
    auto back = syntax::read(printed, "t.new");
    REQUIRE(back);
    CHECK(first_difference(**root, **back).empty());
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

TEST_CASE(
    "a list cell that begins with a column's bare name is an error, so a nested row cannot read") {
    // A row written in today's nested form under a header would read with every field
    // shifted into the wrong column: (k (a a 1) (b b 2)). It does not read.
    CHECK(error_of("(t\n   #columns (a b)\n   (k (a 1) (b 2)))") ==
          "row k: the cell for a, (a 1), begins with the column name a: a cell is its values "
          "alone, and a list that begins with a column's name quotes it ('a' ...) @3:7");
    CHECK(error_of("(t #columns (a b) (k (b 2) (a 1)))") ==
          "row k: the cell for a, (b 2), begins with the column name b: a cell is its values "
          "alone, and a list that begins with a column's name quotes it ('b' ...) @1:22");
    // Quoted, the name is a value; a list beginning with any other symbol is a list.
    CHECK(same_tree("(t #columns (a b) (k 1 ('a' 2)))", "(t (k (a 1) (b a 2)))").empty());
    CHECK(same_tree("(t #columns (a b) (k 1 (c 2)))", "(t (k (a 1) (b c 2)))").empty());
    // The printer quotes such a value, and the table reads back.
    auto root = read_string("(t (k (a 1) (b a 2)) (j (a 3) (b b 4)))", "t.dta");
    REQUIRE(root);
    const std::string printed = syntax::print(**root, {.width = 30});
    INFO(printed);
    CHECK(printed.find("('a' 2)") != std::string::npos);
    CHECK(printed.find("('b' 4)") != std::string::npos);
    auto back = syntax::read(printed, "t.new");
    REQUIRE(back);
    CHECK(first_difference(**root, **back).empty());
}

TEST_CASE("the brief's rule: a dropped cell in a row with children shifts silently") {
    // low_kick's cancels cell (palm) is dropped: the hitbox list slides into cancels and the
    // first child, (crouching 1), into hitbox. Only a shape can see it, at check.
    const char* dropped =
        "(moves\n"
        "   #columns (button startup push cancels hitbox)\n"
        "   (low_kick kick 6 0.15 (0.3 0.1 0.6 0.25) (crouching 1) (guard low)))\n";
    CHECK(same_tree(dropped,
                    "(moves (low_kick (button kick) (startup 6) (push 0.15)\n"
                    "   (cancels 0.3 0.1 0.6 0.25) (hitbox crouching 1) (guard low)))")
              .empty());
    // The strict rule: a row is its key and one cell per column, so the same row is a count
    // error, and so is the headline example's (crouching 1) after the cells.
    const syntax::ReadOptions strict{.trailing_children = false};
    CHECK(error_of(dropped, strict) ==
          "row low_kick has 6 cells, #columns declares 5 (the first extra is (guard low)): a row "
          "is its key and one cell per column @3:4");
    CHECK(error_of("(moves #columns (button cancels hitbox)\n"
                   "   (jab light (jab low_kick) (0.3 1.3 0.55 0.2) (crouching 1)))",
                   strict) ==
          "row jab has 4 cells, #columns declares 3 (the first extra is (crouching 1)): a row is "
          "its key and one cell per column @2:4");
    // Under the strict rule an optional field is a column, with holes where rows lack it.
    auto a = syntax::read(
        "(moves #columns (button crouching cancels hitbox)\n"
        "   (jab      light 1 (jab low_kick) (0.3 1.3 0.55 0.2))\n"
        "   (palm     special _ _ (0.2 0.8 0.8 0.8)))",
        "t.dta", strict);
    REQUIRE(a);
    auto b = read_string(
        "(moves (jab (button light) (crouching 1) (cancels jab low_kick) (hitbox 0.3 1.3 0.55 0.2))"
        "       (palm (button special) (hitbox 0.2 0.8 0.8 0.8)))",
        "t.dta");
    REQUIRE(b);
    CHECK(first_difference(**a, **b).empty());
    // The printer writes every field as a cell, so its tables read under both rules.
    auto moves = read_string(kMovesNested, "moves.dta");
    REQUIRE(moves);
    auto back = syntax::read(syntax::print(**moves), "moves.new", strict);
    REQUIRE(back);
    CHECK(first_difference(**moves, **back).empty());
}

TEST_CASE("an error the parser finds inside a rewritten row is located as written") {
    // The rewrite inserts "(a " and ")" around each cell; the column is mapped back.
    CHECK(error_of("(t\n   #columns (a b)\n   (k 1 2 (x 3 (y 4)) {f)\n") ==
          "'{' from line 3 is closed by ')' @3:25");
    CHECK(error_of("(t #columns (a b) (k 1 99999999999999999999))") ==
          "integer 99999999999999999999 is out of 64-bit range @1:24");
    CHECK(error_of("(t #columns (a b) (k 1 \"abc\n") == "unterminated string @1:24");
}

TEST_CASE("an included file's tables are read too") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "baker_column_tables_include";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "table.dta") << "(goods\n   #columns (price demand)\n   (wheat 2 300))\n";
    auto root = syntax::read("#include table.dta\n(extra 1)\n", (dir / "main.dta").string());
    REQUIRE(root);
    REQUIRE((*root)->try_get("goods.wheat.demand"));
    CHECK((*root)->try_get("goods.wheat.demand")->line() == 3);
    CHECK((*root)->try_get("goods.wheat.demand")->file().str() == (dir / "table.dta").string());
    std::filesystem::remove_all(dir);
}

TEST_CASE("no text crashes the reader: every prefix and many mutations of the samples") {
    const char* samples[] = {"duel-default.columns.dta", "ledger-economy.columns.dta",
                             "stuntline-default.columns.dta", "hearth-default.columns.dta",
                             "contraption-default.columns.dta"};
    const std::string dir =
        std::string(repository_root()) + "/experiments/syntax-column-tables/samples/";
    const char alphabet[] = "()[]{}_#;\"' \n1a-.$";
    uint32_t seed = 12345;
    auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
    size_t read_ok = 0, refused = 0;
    for (const char* name : samples) {
        auto text = read_text_file(dir + name);
        REQUIRE(text);
        auto count = [&](const std::string& t) {
            for (const bool trailing : {true, false}) {
                auto r = syntax::read(t, name, {.trailing_children = trailing});
                ++(r ? read_ok : refused);
                if (!r) CHECK(r.error().line() > 0);
            }
        };
        for (size_t n = 0; n <= text->size(); ++n) count(text->substr(0, n));
        for (int i = 0; i < 2000; ++i) {
            std::string t = *text;
            for (int k = 0; k < 3; ++k) {
                const size_t at = next() % (t.size() + 1);
                const char c = alphabet[next() % (sizeof alphabet - 1)];
                switch (next() % 3) {
                    case 0: t.insert(at, 1, c); break;
                    case 1:
                        if (at < t.size()) t.erase(at, 1);
                        break;
                    default:
                        if (at < t.size()) t[at] = c;
                }
            }
            count(t);
        }
    }
    MESSAGE("read " << read_ok << ", refused with a located error " << refused);
    CHECK(read_ok > 0);
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

TEST_CASE("the printer never begins a file with a byte order mark, which a reader skips") {
    // tests/fuzz_regressions/bom_inside_symbol.bin, as libFuzzer found it on this printer.
    auto root = read_string(" \xEF\xBB\xBFze 2", "t.dta");
    REQUIRE(root);
    const std::string printed = syntax::print(**root);
    CHECK(printed == " \xEF\xBB\xBFze\n2\n");
    auto back = syntax::read(printed, "t.new");
    REQUIRE(back);
    CHECK(first_difference(**root, **back).empty());
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
        {"contraption-default.columns.dta", "stacks/contraption/data/default.dta", true},
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

// --- Write-back into headed files, through a Config ---

namespace {

// A disk in memory, and a Config reading and writing it through HeadedFiles.
struct Disk {
    std::map<std::string, std::string, std::less<>> files;
    syntax::HeadedFiles headed;
    Config cfg{{.tweaks = nullptr}};
    explicit Disk(const syntax::ReadOptions& options = {})
        : headed(
              [this](std::string_view path, std::string& out) {
                  const auto it = files.find(path);
                  if (it == files.end()) return false;
                  out = it->second;
                  return true;
              },
              [this](std::string_view path, std::string_view text) {
                  files[std::string(path)] = text;
                  return true;
              },
              options) {
        headed.attach(cfg);
    }
};

const char* kHeadedMoves =
    "; frame data\n"
    "(moves\n"
    "   #columns (button  startup cancels        hitbox)\n"
    "   (jab      light   4       (jab low_kick) (0.3 1.3 0.55 0.2))\n"
    "   (palm     special 12      _              (0.2 0.8 0.8 0.8)))\n";

}  // namespace

TEST_CASE("write_back of a cell replaces its one token") {
    if constexpr (kShipping) return;  // write-back is inert
    Disk disk;
    disk.files["moves.dta"] = kHeadedMoves;
    REQUIRE(disk.cfg.push_layer("base", "moves.dta"));
    CHECK(disk.cfg.get<int>("moves.jab.startup") == 4);
    REQUIRE(disk.cfg.set("moves.jab.startup", Node(5)));
    REQUIRE(disk.cfg.write_back("moves.jab.startup"));
    CHECK(disk.files["moves.dta"] ==
          "; frame data\n"
          "(moves\n"
          "   #columns (button  startup cancels        hitbox)\n"
          "   (jab      light   5       (jab low_kick) (0.3 1.3 0.55 0.2))\n"
          "   (palm     special 12      _              (0.2 0.8 0.8 0.8)))\n");
    CHECK(disk.cfg.unsaved().empty());
    // A list cell's values, their number changed, inside the cell's own brackets.
    const Node three[] = {Node(Symbol("jab")), Node(Symbol("low_kick")), Node(Symbol("palm"))};
    REQUIRE(disk.cfg.set(Path("moves.jab.cancels"), std::span<const Node>(three)));
    REQUIRE(disk.cfg.write_back("moves.jab.cancels"));
    CHECK(disk.files["moves.dta"].find("(jab      light   5       (jab low_kick palm) (0.3") !=
          std::string::npos);
    CHECK(disk.cfg.unsaved().empty());
    // A whole float over an integer cell keeps the integer form (D183).
    REQUIRE(disk.cfg.set("moves.palm.startup", Node(13.0)));
    REQUIRE(disk.cfg.write_back("moves.palm.startup"));
    CHECK(disk.files["moves.dta"].find("(palm     special 13      _ ") != std::string::npos);
}

TEST_CASE("write_back_to fills a hole, adds a row with holes, and adds a field after the cells") {
    if constexpr (kShipping) return;  // write-back is inert
    Disk disk;
    disk.files["moves.dta"] = kHeadedMoves;
    REQUIRE(disk.cfg.push_layer("base", "moves.dta"));
    // palm has a hole for cancels: the hole becomes the cell.
    REQUIRE(disk.cfg.set("moves.palm.cancels", Node(Symbol("jab"))));
    REQUIRE(disk.cfg.write_back_to("moves.palm.cancels", "base"));
    CHECK(disk.files["moves.dta"].find("(palm     special 12      jab              (0.2 0.8") !=
          std::string::npos);
    // A move the table lacks: a row, its other cells holes.
    REQUIRE(disk.cfg.set("moves.uppercut.startup", Node(5)));
    REQUIRE(disk.cfg.write_back_to("moves.uppercut.startup", "base"));
    CHECK(disk.files["moves.dta"].ends_with("(0.2 0.8 0.8 0.8))\n   (uppercut _ 5 _ _))\n"));
    CHECK(disk.cfg.get<int>("moves.uppercut.startup") == 5);
    // A field that is not a column: a child after the row's cells.
    REQUIRE(disk.cfg.set("moves.jab.guard", Node(Symbol("low"))));
    REQUIRE(disk.cfg.write_back_to("moves.jab.guard", "base"));
    CHECK(disk.files["moves.dta"].find("(0.3 1.3 0.55 0.2) (guard low))\n") != std::string::npos);
    CHECK(disk.cfg.unsaved().empty());
    // After a row's last cell, holes included.
    disk.files["t.dta"] = "(t #columns (a b) (k 1 _) (j 2 3))\n";
    REQUIRE(disk.cfg.push_layer("t", "t.dta"));
    REQUIRE(disk.cfg.set("t.k.x", Node(7)));
    REQUIRE(disk.cfg.write_back_to("t.k.x", "t"));
    CHECK(disk.files["t.dta"] == "(t #columns (a b) (k 1 _ (x 7)) (j 2 3))\n");
}

TEST_CASE("under the strict rule a field with no column is refused, and the file is untouched") {
    if constexpr (kShipping) return;  // write-back is inert
    Disk disk({.trailing_children = false});
    disk.files["moves.dta"] = kHeadedMoves;
    REQUIRE(disk.cfg.push_layer("base", "moves.dta"));
    REQUIRE(disk.cfg.set("moves.jab.guard", Node(Symbol("low"))));
    CHECK(!disk.cfg.write_back_to("moves.jab.guard", "base"));
    REQUIRE(disk.headed.last_error());
    CHECK(disk.headed.last_error()->message() ==
          "row jab has no column for guard: add it to the table's #columns");
    CHECK(disk.files["moves.dta"] == kHeadedMoves);
}

TEST_CASE("promote into a headed file, which writes many entries at once, is refused") {
    if constexpr (kShipping) return;  // write-back is inert
    Disk disk;
    disk.files["moves.dta"] = kHeadedMoves;
    disk.files["agent.dta"] = "(moves (jab (startup 6)) (palm (startup 11)))\n";
    REQUIRE(disk.cfg.push_layer("base", "moves.dta"));
    REQUIRE(disk.cfg.push_layer("agent", "agent.dta"));
    CHECK(!disk.cfg.promote("agent", "base"));
    REQUIRE(disk.headed.last_error());
    CHECK(std::string(disk.headed.last_error()->message()).find("one cell, one row or one entry") !=
          std::string::npos);
    CHECK(disk.files["moves.dta"] == kHeadedMoves);
    // One entry at a time goes through.
    REQUIRE(disk.cfg.set("moves.jab.startup", Node(6)));
    REQUIRE(disk.cfg.write_back_to("moves.jab.startup", "base"));
    CHECK(disk.files["moves.dta"].find("(jab      light   6 ") != std::string::npos);
}
