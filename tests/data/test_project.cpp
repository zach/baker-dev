// Project files (D172).
#include <doctest.h>
#include <baker/project.hpp>
#include <baker/std_interop.hpp>

#include "temp_dir.hpp"

#include <string>
#include "ok.hpp"

using namespace baker;
using baker::test::ok;
using baker::test::TempDir;

namespace {

const char* const kProject =
    "; the stack the game loads\n"
    "(project\n"
    "   (pack_levels engine game task)\n"
    "   (include_dirs \"data/shared\")\n"
    "   (defines PC DEMO)\n"
    "   (layers\n"
    "      (default \"data/default.dta\")\n"
    "      (local \"data/local.dta\" optional)\n"
    "      (experiment \"data/experiment.dta\" optional disabled)))\n";

}  // namespace

TEST_CASE("read_project reads the layers, levels, include dirs, and defines") {
    TempDir dir;
    const std::string file = dir.write("baker-project.dta", kProject);
    const Project p = ok(read_project(file));
    CHECK(p.file == Symbol(file));
    REQUIRE(p.layers.size() == 3);
    CHECK(p.layers[0].name == Symbol("default"));
    CHECK(p.layers[0].path == Symbol(dir.path("data/default.dta")));
    CHECK_FALSE(p.layers[0].optional);
    CHECK(p.layers[1].optional);
    CHECK(p.layers[2].optional);
    CHECK_FALSE(p.layers[2].enabled);
    REQUIRE(p.pack_levels.size() == 3);
    CHECK(p.pack_levels[1] == Symbol("game"));
    REQUIRE(p.include_dirs.size() == 1);
    CHECK(p.include_dirs[0] == Symbol(dir.path("data/shared")));
    REQUIRE(p.defines.size() == 2);
    CHECK(p.defines[0] == Symbol("PC"));
}

TEST_CASE("open_project sets the session and pushes the layers, leaving out what it skips") {
    TempDir dir;
    const std::string file = dir.write("baker-project.dta", kProject);
    dir.write("data/shared/common.dta", "(speed 3)\n");
    dir.write("data/default.dta",
              "(pack base (level game) (version \"1.0\"))\n"
              "#include common.dta\n"
              "#ifdef PC (input mouse) #else (input pad) #endif\n");
    dir.write("data/local.dta", "(speed 9)\n");
    dir.write("data/experiment.dta", "(speed 7)\n");
    const Project p = ok(read_project(file));

    Config cfg({.tweaks = nullptr});
    REQUIRE(open_project(cfg, p));
    CHECK(cfg.get<int>("speed") == 9);  // local's; the experiment is disabled
    CHECK(cfg.get<Symbol>("input") == Symbol("mouse"));
    CHECK(cfg.layers().size() == 4);  // three and scratch
    CHECK_FALSE(cfg.layer_enabled("experiment"));
    CHECK(cfg.read_session().pack_of(Symbol(dir.path("data/default.dta"))));

    Config shared({.tweaks = nullptr});
    REQUIRE(open_project(shared, p, {.skip = {"local"}}));
    CHECK(shared.get<int>("speed") == 3);  // from the include directory
    CHECK(shared.layers().size() == 3);
}

TEST_CASE("read_project reads an unsaved buffer through a reader") {
    const char* const unsaved = "(project (layers (default \"default.dta\")))";
    const LoadSession::Reader reader = [&](std::string_view path, TextSink out) {
        if (path != "proj/baker-project.dta") return false;
        out(unsaved);
        return true;
    };
    const Project p = ok(read_project("proj/baker-project.dta", reader));
    REQUIRE(p.layers.size() == 1);
    CHECK(p.layers[0].path == Symbol("proj/default.dta"));
}

TEST_CASE("a project file's mistakes are located") {
    TempDir dir;
    struct Case {
        const char* text;
        int line;
        const char* says;
    };
    const Case cases[] = {
        {"(project\n (layers (default \"d.dta\"))\n (levels a b))", 3, "has no 'levels'"},
        {"(project\n (layers\n  (default)))", 3, "a layer is"},
        {"(project\n (layers\n  (default \"d.dta\" optinal)))", 3, "only optional and disabled"},
        {"(project\n (layers\n  (a \"x.dta\")\n  (a \"y.dta\")))", 4, "two layers named 'a'"},
        {"(project\n (pack_levels game))", 1, "needs (layers ...)"},
        {"(project\n (pack_levels (game)) (layers))", 2, "lists names or paths"},
        {"(layers (default \"d.dta\"))", 1, "holds (project"},
        {"(project\n #include other.dta\n (layers))", 2, "may include no files"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.text);
        auto p = read_project(dir.write("baker-project.dta", c.text));
        REQUIRE_FALSE(p);
        CHECK(p.error().line() == c.line);
        CHECK(std::string(p.error().message()).find(c.says) != std::string::npos);
    }
    CHECK_FALSE(read_project(dir.path("missing.dta")));
}
