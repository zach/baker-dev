// Directives and macros: include, merge, conditionals, defines, autorun.
#include <doctest.h>
#include <moira/parse.hpp>
#include <moira/std_interop.hpp>
#include <moira/write.hpp>

#include <map>
#include <string>

using namespace moira;

namespace {

// A LoadSession over an in-memory file system.
struct Files {
    std::map<std::string, std::string> files;
    std::vector<std::string> reads;
    LoadSession session;

    Files() {
        session.set_reader([this](std::string_view path, TextOut out) {
            reads.emplace_back(path);
            auto it = files.find(std::string(path));
            if (it == files.end()) return false;
            out(it->second);
            return true;
        });
    }
    std::string load(const std::string& path) {
        auto r = read_file(path, session);
        REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : to_string(r.error())));
        return to_string(**r);
    }
    Error fail(const std::string& path) {
        auto r = read_file(path, session);
        REQUIRE(!r.has_value());
        return r.error();
    }
};

std::string text(std::string_view src) {
    auto r = read_string(src, "t.dta");
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : to_string(r.error())));
    return to_string(**r);
}
Error error(std::string_view src) {
    auto r = read_string(src, "t.dta");
    REQUIRE(!r.has_value());
    return r.error();
}

}  // namespace

TEST_CASE("macros splice their nodes") {
    CHECK(text("[TRUE 1]\n[RED 1 0 0]\n(flag TRUE) (color RED)") == "((flag 1) (color 1 0 0))");
    CHECK(text("[NOTHING]\n(a NOTHING b)") == "((a b))");
    CHECK(text("[A 1]\n[B A A]\n(x B)") == "((x 1 1))");  // expanded while defining B
    CHECK(text("[X 1]\n(x 'X' X)") == "((x X 1))");       // quoted symbols never expand
    CHECK(text("[A 1]\n[A 2]\n(a A)") == "((a 2))");      // redefinition, name not expanded
    CHECK(text("(TAG 1)\n[TAG t]\n(TAG 2)") == "((TAG 1) (t 2))");  // only after definition
}

TEST_CASE("macro expansion shares arrays") {
    auto root = *read_string("[HANDLER (hit {fall_down})]\n(object1 HANDLER)\n(object2 HANDLER)");
    const Array* h1 = (*(*root)[0].array())[1].array();
    const Array* h2 = (*(*root)[1].array())[1].array();
    CHECK(h1 == h2);
    CHECK(to_string(*root) == "((object1 (hit {fall_down})) (object2 (hit {fall_down})))");
}

TEST_CASE("[ ] defines a macro only at a file's top level") {
    CHECK(text("(a [b c])") == "((a [b c]))");
    CHECK(text("[M [p q]]\n(a M)") == "((a [p q]))");
    CHECK(error("[1 2]").message() == "macro definition needs a symbol name");
    CHECK(error("[]").message() == "macro definition needs a symbol name");
    // A spliced property at top level would print back as a macro definition.
    CHECK(error("[M [p q]]\nM").code() == ErrorCode::Syntax);
}

TEST_CASE("#define and #undef") {
    CHECK(text("#define kMax (4)\n(players kMax)") == "((players 4))");
    CHECK(text("#define PAIR (1 2)\n(p PAIR)") == "((p 1 2))");
    CHECK(text("#define FLAG\n(a FLAG)") == "((a FLAG))");  // a flag is not a macro
    // The body must start on the same line; otherwise the define is a flag.
    CHECK(text("#define FLAG\n(next 1)") == "((next 1))");
    CHECK(text("#define X (1)\n#undef X\n(a X)") == "((a X))");
    CHECK(error("#define").code() == ErrorCode::Directive);
    CHECK(error("#define (a)").message() == "#define needs a name");
    CHECK(error("#undef 5").message() == "#undef needs a name");
}

TEST_CASE("conditionals") {
    CHECK(text("#define A\n#ifdef A (yes) #else (no) #endif") == "((yes))");
    CHECK(text("#ifdef A (yes) #else (no) #endif") == "((no))");
    CHECK(text("#ifndef A (yes) #endif") == "((yes))");
    CHECK(text("[A 1]\n#ifdef A (macro_counts) #endif") == "((macro_counts))");
    // Nested, including inside skipped regions, and spanning brackets.
    CHECK(text("#define A\n"
               "#ifdef A\n"
               "  #ifdef B (ab) #else (a_not_b) #endif\n"
               "#else\n"
               "  #ifdef B (b) #else (neither) #endif\n"
               "#endif") == "((a_not_b))");
    CHECK(text("(list 1\n#ifdef X\n 2)\n#else\n 3)\n#endif") == "((list 1 3))");
    // Skipped regions may contain anything that lexes, including unknown directives.
    CHECK(text("#ifdef NOPE (unbalanced #bogus #include nowhere #endif (ok)") == "((ok))");
    // The name of #ifdef is never macro-expanded.
    CHECK(text("[A B]\n#ifdef A (a) #endif") == "((a))");

    CHECK(error("#else").message() == "#else without #ifdef");
    CHECK(error("#endif").message() == "#endif without #ifdef");
    Error e = error("(a)\n#ifdef X\n(b)");
    CHECK(e.message() == "#ifdef has no matching #endif");
    CHECK(e.line() == 2);
    CHECK(error("#ifdef X #else #else #endif").message() == "second #else for one #ifdef");
    CHECK(error("#ifdef (a) #endif").message() == "#ifdef needs a name");
}

TEST_CASE("unknown directives are errors") {
    Error e = error("(a)\n#bogus x");
    CHECK(e.message() == "unknown directive '#bogus'");
    CHECK(e.code() == ErrorCode::Directive);
    CHECK(e.line() == 2);
    CHECK(error("#").message() == "unknown directive '#'");
}

TEST_CASE("#include splices files, relative to the including file") {
    Files fs;
    fs.files["game/main.dta"] = "(first)\n#include sub/part.dta\n(last)";
    fs.files["game/sub/part.dta"] = "(part 1)\n#include \"leaf.dta\"";
    fs.files["game/sub/leaf.dta"] = "(leaf)";
    CHECK(fs.load("game/main.dta") == "((first) (part 1) (leaf) (last))");

    // Inside an array, the included nodes become children.
    fs.files["in_array.dta"] = "(list 0\n#include items.dta\n 4)";
    fs.files["items.dta"] = "1 2 3";
    CHECK(fs.load("in_array.dta") == "((list 0 1 2 3 4))");
}

TEST_CASE("included arrays carry their own file and line") {
    Files fs;
    fs.files["a.dta"] = "#include b.dta";
    fs.files["b.dta"] = "\n\n(from_b)";
    auto root = *read_file("a.dta", fs.session);
    CHECK((*root)[0].array()->file() == Symbol("b.dta"));
    CHECK((*root)[0].array()->line() == 3);
}

TEST_CASE("macros and defines persist across files in a session") {
    Files fs;
    fs.files["macros.dta"] = "[TRUE 1]\n#define DEBUG";
    fs.files["use.dta"] = "#include macros.dta\n(flag TRUE)\n#ifdef DEBUG (debug) #endif";
    CHECK(fs.load("use.dta") == "((flag 1) (debug))");
    fs.files["later.dta"] = "(still TRUE)";
    CHECK(fs.load("later.dta") == "((still 1))");
    CHECK(fs.session.is_defined("DEBUG"));
    CHECK(fs.session.macro("TRUE") != nullptr);
    CHECK(fs.session.macro("DEBUG") == nullptr);
}

TEST_CASE("include search falls back to include_dirs") {
    Files fs;
    fs.session.add_include_dir("shared");
    fs.session.add_include_dir("other");
    fs.files["levels/l1.dta"] = "#include common.dta";
    fs.files["other/common.dta"] = "(common)";
    CHECK(fs.load("levels/l1.dta") == "((common))");
    CHECK(fs.reads == std::vector<std::string>{"levels/l1.dta", "levels/common.dta",
                                               "shared/common.dta", "other/common.dta"});
}

TEST_CASE("include errors") {
    Files fs;
    fs.files["a.dta"] = "(a)\n#include missing.dta";
    Error e = fs.fail("a.dta");
    CHECK(e.code() == ErrorCode::Io);
    CHECK(e.message() == "cannot read included file 'missing.dta'");
    CHECK(e.file() == Symbol("a.dta"));
    CHECK(e.line() == 2);

    fs.files["opt.dta"] = "(a)\n#include_opt missing.dta\n(b)";
    CHECK(fs.load("opt.dta") == "((a) (b))");

    fs.files["self.dta"] = "#include self.dta";
    CHECK(fs.fail("self.dta").message() == "'self.dta' includes itself");
    fs.files["x.dta"] = "#include y.dta";
    fs.files["y.dta"] = "#include x.dta";
    CHECK(fs.fail("x.dta").code() == ErrorCode::Directive);

    fs.files["bad_inner.dta"] = "#include broken.dta";
    fs.files["broken.dta"] = "\n(oops";
    e = fs.fail("bad_inner.dta");
    CHECK(e.file() == Symbol("broken.dta"));
    CHECK(e.line() == 2);

    fs.files["noarg.dta"] = "#include (x)";
    CHECK(fs.fail("noarg.dta").message() == "#include needs a file path");

    // An include-only-once loop that is not a cycle still hits the include budget.
    fs.session.limits.max_includes = 10;
    fs.files["many.dta"] =
        "#include leaf.dta\n#include leaf.dta\n#include leaf.dta\n"
        "#include leaf.dta\n#include leaf.dta\n#include leaf.dta\n"
        "#include leaf.dta\n#include leaf.dta\n#include leaf.dta\n"
        "#include leaf.dta\n#include leaf.dta";
    fs.files["leaf.dta"] = "(leaf)";
    CHECK(fs.fail("many.dta").code() == ErrorCode::Limit);
}

TEST_CASE("#merge keeps existing values and adds missing ones") {
    Files fs;
    // The GDC slide example: existing leaves win, missing arrays are added.
    fs.files["slide.dta"] = "(a (b 1))\n#merge more.dta";
    fs.files["more.dta"] = "(a (b 2) (c 2))";
    CHECK(fs.load("slide.dta") == "((a (b 1) (c 2)))");

    fs.files["app.dta"] =
        "(renderer (screen_size 1920 1080) (shadows (resolution 4096)))\n"
        "#merge default.dta";
    fs.files["default.dta"] =
        "(renderer (screen_size 1280 720) (vsync 1) (shadows (enabled 1) (resolution 2048)))\n"
        "(audio (channels 64))\n"
        "loose_symbol\n"
        "(1 2 3)";
    CHECK(fs.load("app.dta") ==
          "((renderer (screen_size 1920 1080) (shadows (resolution 4096) (enabled 1)) (vsync 1)) "
          "(audio (channels 64)) (1 2 3))");

    // Inside an array, #merge merges into that array.
    fs.files["inner.dta"] = "(cfg (a 1)\n#merge extra.dta\n)";
    fs.files["extra.dta"] = "(a 9) (b 2)";
    CHECK(fs.load("inner.dta") == "((cfg (a 1) (b 2)))");
}

TEST_CASE("#merge never mutates shared arrays") {
    Files fs;
    fs.files["m.dta"] = "[BASE (cfg (a 1))]\n(one BASE)\n(two BASE\n#merge add.dta\n)";
    fs.files["add.dta"] = "(cfg (b 2))";
    CHECK(fs.load("m.dta") == "((one (cfg (a 1))) (two (cfg (a 1) (b 2))))");
}

TEST_CASE("#autorun collects commands") {
    LoadSession s;
    auto r = read_string("(a)\n#autorun {setup 1}\n(b)\n#autorun {setup 2}", "t.dta", s);
    REQUIRE(r);
    CHECK(to_string(**r) == "((a) (b))");
    REQUIRE(s.autorun().size() == 2);
    CHECK(s.autorun()[0].type() == Type::Command);
    CHECK(to_string(s.autorun()[1]) == "{setup 2}");
    CHECK(error("#autorun (x)").message() == "#autorun needs a {command}");
}

TEST_CASE("size limits stop macro blow-ups") {
    LoadSession s;
    s.limits.max_nodes = 10000;
    std::string src = "[A x x]\n";
    char prev = 'A';
    for (char c = 'B'; c <= 'Z'; ++c, ++prev)
        src += std::string("[") + c + ' ' + prev + ' ' + prev + "]\n";
    src += "(boom Z)";
    auto r = read_string(src, "bomb.dta", s);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == ErrorCode::Limit);

    // Shared-array chains stay small in memory but their height is still bounded.
    std::string chain = "[M0 (x)]\n";
    for (int i = 1; i <= 300; ++i)
        chain += "[M" + std::to_string(i) + " (M" + std::to_string(i - 1) + ")]\n";
    auto deep = read_string(chain);
    REQUIRE_FALSE(deep.has_value());
    CHECK(deep.error().code() == ErrorCode::Limit);
}
