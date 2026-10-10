// The printer: spelling of every value, layout, and the round-trip guarantee.
#include <doctest.h>
#include <moira/parse.hpp>
#include <moira/write.hpp>

#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <random>
#include <string>

using namespace moira;

namespace {
Ref<Array> parse(std::string_view text) {
    auto r = try_read_string(text, "t.dta");
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : to_string(r.error())));
    return *r;
}
void check_round_trip(const Array& root) {
    const std::string printed = to_dta(root);
    auto again = try_read_string(printed, "printed.dta");
    REQUIRE_MESSAGE(again.has_value(), printed);
    CHECK_MESSAGE(structurally_equal(root, **again), printed);
}
}  // namespace

TEST_CASE("atoms print in their reparseable spelling") {
    CHECK(to_string(Node(42)) == "42");
    CHECK(to_string(Node(-7)) == "-7");
    CHECK(to_string(Node(12.75f)) == "12.75");
    CHECK(to_string(Node(5.0f)) == "5.0");
    CHECK(to_string(Node(-0.0f)) == "-0.0");
    CHECK(to_string(Node(1e20f)) == "1e+20");
    CHECK(to_string(Node(0.001f)) == "0.001");
    CHECK(to_string(Node(Symbol("price"))) == "price");
    CHECK(to_string(Node(Symbol("bacon and eggs"))) == "'bacon and eggs'");
    CHECK(to_string(Node(Symbol(""))) == "''");
    CHECK(to_string(Node(Symbol("12"))) == "'12'");
    CHECK(to_string(Node(Symbol("1e5"))) == "'1e5'");
    CHECK(to_string(Node(Symbol("99999999999"))) == "'99999999999'");
    CHECK(to_string(Node(Symbol("$x"))) == "'$x'");
    CHECK(to_string(Node(Symbol("#if"))) == "'#if'");
    CHECK(to_string(Node(Symbol("/*x"))) == "'/*x'");
    CHECK(to_string(Node(Symbol("a/*x"))) == "a/*x");
    CHECK(to_string(Node(Symbol("semi;colon"))) == "'semi;colon'");
    CHECK(to_string(Node(Symbol("don't"))) == "don't");
    CHECK(to_string(Node(Symbol("-"))) == "-");
    CHECK(to_string(Node(Symbol("\xEF\xBB\xBFze"))) == "\xEF\xBB\xBFze");
    CHECK(to_dta(*array(array("\xEF\xBB\xBFze''", 1))) == "(\xEF\xBB\xBFze'' 1)\n");
    CHECK(to_dta(*Array::make_copy(std::vector<Node>{Node(Symbol("\xEF\xBB\xBFze''"))})) ==
          " \xEF\xBB\xBFze''\n");
    CHECK(to_string(Node(str("say \"hi\"\nbye"))) == R"("say \qhi\q\nbye")");
    CHECK(to_string(Node(str(R"(C:\games)"))) == R"("C:\games")");
    CHECK(to_string(Node::var("game_time")) == "$game_time");
    static const FuncRecord rec{"+"};
    CHECK(to_string(Node::func(&rec)) == "+");
    CHECK(to_string(Node::object(ObjectHandle::make(2, 7))) == "'<object 2:7>'");
}

TEST_CASE("arrays print compactly and break when too wide") {
    auto root = parse("(menu ('bacon and eggs' (price 12.75) (calories 120)) (x {cmd $v} [prop]))");
    CHECK(to_string((*root)[0]) ==
          "(menu ('bacon and eggs' (price 12.75) (calories 120)) (x {cmd $v} [prop]))");
    CHECK(to_dta(*root, {.indent = 2, .width = 40}) ==
          "(menu\n"
          "  ('bacon and eggs'\n"
          "    (price 12.75)\n"
          "    (calories 120))\n"
          "  (x {cmd $v} [prop]))\n");
    CHECK(to_dta(*parse("(a 1) b 3")) == "(a 1)\nb\n3\n");
    CHECK(to_dta(*parse("")).empty());
}

TEST_CASE("printing round-trips hand-picked trees") {
    for (const char* src : {
             "(a 1 -2 3.5 -0.0 1e-30 3.4e38 sym 'q s' \"str\" $v {c 1} (n [p]))",
             "('' '\"' \"'\" \"\\q\" \"a\\\\nb\" \"\\\\\" 'x\ny' \"x\ny\")",
             "(0x7FFFFFFF 0xFFFFFFFF -2147483648 '-' '+' '.' '1.' '0x1' '-1' 'inf')",
             "({{nearest_object $p} suffer_damage} [a [b [c]]])",
         }) {
        CAPTURE(src);
        check_round_trip(*parse(src));
    }
}

TEST_CASE("printing round-trips random trees") {
    std::mt19937 rng(12345);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
    const char alphabet[] = "abz09 _-+.'\"$#;()[]{}\\/*\nq";
    auto random_text = [&] {
        std::string s;
        for (int i = pick(6); i > 0; --i)
            s += alphabet[pick(static_cast<int>(sizeof alphabet - 1))];
        return s;
    };
    std::function<Node(int)> gen = [&](int depth) -> Node {
        switch (pick(depth > 4 ? 6 : 9)) {
            case 0: return Node(static_cast<int32_t>(rng()));
            case 1: {
                float f = std::bit_cast<float>(static_cast<uint32_t>(rng()));
                return std::isfinite(f) ? Node(f) : Node(1.5f);
            }
            case 2:
            case 3: {
                // A symbol that needs quotes cannot also contain a quote (D13).
                std::string s = random_text();
                for (char& c : s)
                    if (c == '\'') c = '_';
                return Node(Symbol(s));
            }
            case 4: {
                // Strings cannot hold a literal backslash followed by n or q (D13).
                std::string s = random_text();
                for (size_t i = 0; i + 1 < s.size(); ++i)
                    if (s[i] == '\\' && (s[i + 1] == 'n' || s[i + 1] == 'q')) s[i] = '/';
                return Node(str(s));
            }
            case 5: {
                std::string name = "v" + random_text();
                for (char& c : name)
                    if (static_cast<unsigned char>(c) <= 0x20 ||
                        std::string_view("()[]{}\";").find(c) != std::string_view::npos)
                        c = '_';
                return Node::var(Symbol(name));
            }
            default: {
                std::vector<Node> kids;
                for (int i = pick(5); i > 0; --i) kids.push_back(gen(depth + 1));
                const Type kinds[] = {Type::Array, Type::Command, Type::Property};
                return Node(Array::make_from(kids), kinds[pick(3)]);
            }
        }
    };
    for (int round = 0; round < 300; ++round) {
        std::vector<Node> top;
        for (int i = pick(6); i > 0; --i) {
            Node n = gen(0);
            if (n.type() == Type::Property) n = Node(Ref<Array>(n.mutable_array()));  // D13
            top.push_back(n);
        }
        check_round_trip(*Array::make_from(top));
    }
}

TEST_CASE("write_file writes the file form") {
    const auto path = (std::filesystem::temp_directory_path() / "moira_write_test.dta").string();
    auto root = parse("(a 1) (b \"two\")");
    REQUIRE(try_write_file(path, *root).has_value());
    auto back = try_read_file(path);
    REQUIRE(back.has_value());
    CHECK(structurally_equal(*root, **back));
    std::remove(path.c_str());
    CHECK(try_write_file("/definitely/not/a/dir/x.dta", *root).error().code == ErrorCode::Io);
}

TEST_CASE("preview is bounded on shared trees") {
    std::string src = "[M0 (x x)]\n";
    for (int i = 1; i <= 60; ++i)
        src += "[M" + std::to_string(i) + " (M" + std::to_string(i - 1) + " M" +
               std::to_string(i - 1) + ")]\n";
    src += "(top M60)";
    auto root = parse(src);
    const std::string p = detail::preview((*root)[0], 40);
    CHECK(p.size() == 40);
    CHECK(p.ends_with("..."));
}
