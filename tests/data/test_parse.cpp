// Lexing and parsing: tokens, bounds checks, nesting, and located errors.
#include <doctest.h>
#include <moira/parse.hpp>
#include <moira/write.hpp>

#include "text.hpp"

#include <string>

using namespace moira;

namespace {
Ref<Array> parse(std::string_view text) {
    auto r = try_read_string(text, "t.dta");
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : to_string(r.error())));
    return *r;
}
Error parse_error(std::string_view text) {
    auto r = try_read_string(text, "t.dta");
    REQUIRE_MESSAGE(!r.has_value(), "expected an error for: " << text);
    return r.error();
}
const Node& only(const Ref<Array>& root) {
    REQUIRE(root->size() == 1);
    return (*root)[0];
}
}  // namespace

TEST_CASE("menu example parses with file and line on every array") {
    auto root = parse(
        "; a comment\n"
        "\n"
        "(menu\n"
        "  ('bacon and eggs'\n"
        "    (price 12.75)\n"
        "    (calories 120))\n"
        "  ('fruit and cereal'\n"
        "    (price 11.75)\n"
        "    (calories 12)))\n");
    const Array& menu = *only(root).array();
    CHECK(menu.line() == 3);
    CHECK(menu.file() == Symbol("t.dta"));
    CHECK(menu[0].try_sym() == Symbol("menu"));
    const Array& bacon = *menu[1].array();
    CHECK(bacon.line() == 4);
    CHECK(bacon[0].try_sym() == Symbol("bacon and eggs"));
    CHECK(bacon[1].array()->line() == 5);
    CHECK((*bacon[1].array())[1].try_float() == 12.75f);
    CHECK((*bacon[2].array())[1].try_int() == 120);
    CHECK(root->line() == 1);
}

TEST_CASE("integers") {
    CHECK(only(parse("42")).try_int() == 42);
    CHECK(only(parse("-7")).try_int() == -7);
    CHECK(only(parse("+7")).try_int() == 7);
    CHECK(only(parse("007")).try_int() == 7);
    CHECK(only(parse("0x1F")).try_int() == 31);
    CHECK(only(parse("0XfF")).try_int() == 255);
    CHECK(only(parse("-0x10")).try_int() == -16);
    // Hex denotes its unsigned value (D18): 0xFFFFFFFF is 4294967295, not -1.
    CHECK(only(parse("0xFFFFFFFF")).try_int() == 4294967295);
    CHECK(only(parse("0x80000000")).try_int() == 2147483648);
    CHECK(only(parse("0x7FFFFFFFFFFFFFFF")).try_int() == INT64_MAX);
    CHECK(only(parse("0x0000000000000000001")).try_int() == 1);
    CHECK(parse_error("0x8000000000000000").code == ErrorCode::Range);
    CHECK(parse_error("0xFFFFFFFFFFFFFFFFF").code == ErrorCode::Range);
    CHECK(only(parse("2147483648")).try_int() == 2147483648);
    CHECK(only(parse("9223372036854775807")).try_int() == INT64_MAX);
    CHECK(only(parse("-9223372036854775808")).try_int() == INT64_MIN);
    CHECK(parse_error("9223372036854775808").code == ErrorCode::Range);
    CHECK(parse_error("-9223372036854775809").code == ErrorCode::Range);
    CHECK(parse_error("99999999999999999999999").code == ErrorCode::Range);
}

TEST_CASE("floats") {
    CHECK(only(parse("12.75")).try_float() == 12.75f);
    CHECK(only(parse("1e-3")).type() == Type::Float);
    CHECK(only(parse("1e-3")).try_float() == 1e-3);
    CHECK(only(parse("0.1")).try_float() == 0.1);
    CHECK(only(parse("1e39")).try_float() == 1e39);
    CHECK(only(parse("4.9e-324")).try_float() > 0.0);  // subnormals are fine
    CHECK(only(parse(".5")).try_float() == 0.5f);
    CHECK(only(parse("-.5")).try_float() == -0.5f);
    CHECK(only(parse("5.")).try_float() == 5.0f);
    CHECK(only(parse("2.5E2")).try_float() == 250.0f);
    CHECK(only(parse("-0.0")).float_bits() == 0x8000000000000000u);
    CHECK(parse_error("1e309").code == ErrorCode::Range);
    CHECK(parse_error("-1e309").code == ErrorCode::Range);
    CHECK(parse_error("1e-400").code == ErrorCode::Range);  // rounds to zero
}

TEST_CASE("atoms that are not numbers are symbols") {
    for (const char* s : {"-", "+", ".", "--", "1e", "0x", "0xZZ", "1.2.3", "inf", "nan", "-inf",
                          "1_000", "12abc", "a'b", "x/y", "../a.dta", "a/*b", "*"}) {
        CAPTURE(s);
        CHECK(only(parse(s)).try_sym() == Symbol(s));
    }
}

TEST_CASE("quoted symbols, strings, and variables") {
    CHECK(only(parse("'odd sym'")).try_sym() == Symbol("odd sym"));
    CHECK(only(parse("''")).try_sym() == Symbol());
    CHECK(only(parse("'12'")).try_sym() == Symbol("12"));
    CHECK(only(parse("'multi\nline'")).try_sym() == Symbol("multi\nline"));
    CHECK(only(parse("\"bacon\"")).try_str() == "bacon");
    CHECK(only(parse("\"\"")).try_str() == "");
    CHECK(only(parse(R"("say \qhi\q")")).try_str() == "say \"hi\"");
    CHECK(only(parse(R"("a\nb")")).try_str() == "a\nb");
    CHECK(only(parse(R"("C:\games\x")")).try_str() == R"(C:\games\x)");
    CHECK(only(parse(R"("end\")")).try_str() == "end\\");
    CHECK(only(parse("\"two\nlines\"")).try_str() == "two\nlines");
    CHECK(only(parse("$game_time")).var_name() == Symbol("game_time"));
    CHECK(parse_error("$").code == ErrorCode::Syntax);
    CHECK(parse_error("\"open").message == "unterminated string");
    CHECK(parse_error("'open").code == ErrorCode::Syntax);
}

TEST_CASE("comments and whitespace") {
    auto root = parse("; line comment\n(a /* block\ncomment */ 1) ; trailing\n/**/(b)");
    REQUIRE(root->size() == 2);
    CHECK(to_string(*root) == "((a 1) (b))");
    CHECK((*root)[1].array()->line() == 4);
    CHECK(parse_error("(a) /* never closed\n\n").line == 1);
    CHECK(to_string(*parse("\xEF\xBB\xBF(bom)")) == "((bom))");
    auto crlf = parse("(a)\r\n(b)\r\n\r\n(c)");
    CHECK((*crlf)[2].array()->line() == 4);
    CHECK(to_string(*parse("(\x01\x7f\t\v\f x)")) == "((\x7f x))");
    CHECK(parse("")->empty());
    CHECK(parse("   \n ; only a comment")->empty());
}

TEST_CASE("brackets") {
    auto root = parse("(a {b c} [d])");
    const Array& a = *only(root).array();
    CHECK(a[1].type() == Type::Command);
    CHECK(a[2].type() == Type::Property);
    CHECK(to_string(*parse("() {} (())")) == "(() {} (()))");

    Error e = parse_error("(a\n(b\n)");
    CHECK(e.message == "'(' is never closed");
    CHECK(e.line == 1);
    e = parse_error("(a\n {b)\n");
    CHECK(e.message == "'{' from line 2 is closed by ')'");
    CHECK(e.line == 2);
    CHECK(parse_error("(a))").message == "unexpected ')'");
    CHECK(parse_error("]").message == "unexpected ']'");
}

TEST_CASE("nesting is bounded") {
    std::string ok(256, '('), deep(257, '(');
    ok += std::string(256, ')');
    deep += std::string(257, ')');
    CHECK(parse(ok)->height() == 257);
    CHECK(parse_error(deep).code == ErrorCode::Limit);
    // An unterminated pathological input fails fast rather than recursing.
    CHECK(parse_error(std::string(100000, '(')).code == ErrorCode::Limit);
}

TEST_CASE("errors format with file and line") {
    Error e = parse_error("(a 1)\n(b 0x1FFFFFFFFFFFFFFFF)");
    CHECK(e.file == Symbol("t.dta"));
    CHECK(e.line == 2);
    CHECK(to_string(e) == "moira: integer 0x1FFFFFFFFFFFFFFFF is out of 64-bit range  (t.dta:2)");
}

#if MOIRA_EXCEPTIONS
TEST_CASE("throwing twins") {
    CHECK(to_string(*read_string("(x 1)")) == "((x 1))");
    CHECK_THROWS_AS((void)read_string("(x"), Exception);
    CHECK_THROWS_AS((void)read_file("/definitely/not/here.dta"), Exception);
}
#endif

TEST_CASE("missing file is an io error") {
    auto r = try_read_file("/definitely/not/here.dta");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == ErrorCode::Io);
}

TEST_CASE("include paths are handled lexically") {
    using detail::normalize_path;
    CHECK(normalize_path("a/./b/../c.dta") == "a/c.dta");
    CHECK(normalize_path("game/sub/part.dta") == "game/sub/part.dta");
    CHECK(normalize_path("a//b") == "a/b");
    CHECK(normalize_path("../x/../../y.dta") == "../../y.dta");
    CHECK(normalize_path("/a/../../b.dta") == "/b.dta");
    CHECK(normalize_path("C:\\games\\..\\cfg\\x.dta") == "C:/cfg/x.dta");
    CHECK(normalize_path("./") == ".");
    CHECK(normalize_path("<string>") == "<string>");
    CHECK(normalize_path("") == "");
    CHECK(detail::is_absolute_path("/a"));
    CHECK(detail::is_absolute_path("C:\\a"));
    CHECK_FALSE(detail::is_absolute_path("C:a"));
    CHECK_FALSE(detail::is_absolute_path("a/b"));
    CHECK(detail::parent_path("a/b/c.dta") == "a/b");
    CHECK(detail::parent_path("c.dta") == "");
    CHECK(detail::parent_path("/c.dta") == "/");
    CHECK(detail::join_path("", "x.dta") == "x.dta");
    CHECK(detail::join_path("dir", "x.dta") == "dir/x.dta");
    CHECK(detail::join_path("dir/", "x.dta") == "dir/x.dta");
}
