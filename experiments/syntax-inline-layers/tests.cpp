// The experiment's tests: the corpus round trip through the block-aware reader, the
// converter on each form a region takes, the check, and blocks back to #ifdef.
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/std_interop.hpp>

#include <doctest.h>

#include <string>

using namespace baker;
using namespace baker::experiments;

namespace {

std::string kinds(const syntax::Conversion& c) {
    std::string out;
    for (const syntax::Region& r : c.regions)
        out += (out.empty() ? "" : ", ") + std::string(syntax::describe(r.kind));
    return out;
}

// The conversion of `text`, checked under every define set; the sets that differ.
std::vector<std::string> differs(std::string_view text, int* order_only = nullptr) {
    const syntax::Conversion c = syntax::to_blocks(text);
    return syntax::check_conversion("mem/t.dta", text, c.text, nullptr, order_only);
}

}  // namespace

TEST_CASE("every corpus file survives the round trip through the block-aware reader") {
    const auto failures = round_trip_corpus(syntax::print, syntax::read);
    for (const RoundTrip& f : failures) {
        INFO(f.file << ": " << f.problem);
        CHECK(f.stage == RoundTrip::Stage::Baker);
    }
    CHECK(corpus_files().size() > 50);
}

TEST_CASE("ui_menu's conditionals become two blocks that merge as the #ifdefs did") {
    const std::string path = std::string(repository_root()) + "/tests/corpus/ui_menu.dta";
    auto text = read_text_file(path);
    REQUIRE(text);
    const syntax::Conversion c = syntax::to_blocks(*text);
    CHECK(kinds(c) == "converted, converted");
    CHECK(c.blocks == std::vector<std::string>{"has_vocals", "not_demo_build"});
    // The #else branch sets only what the defined one sets: it stays, as the default.
    CHECK(c.text.find("(vocals_panel (lanes 0))") != std::string::npos);
    CHECK(c.text.find("#ifdef") == std::string::npos);
    CHECK(c.text.find("(#layer (has_vocals (when HAS_VOCALS))\n"
                      "   (vocals_panel (lanes 3) (pitch_arrow TRUE)))") != std::string::npos);
    CHECK(
        c.text.find("(#layer (not_demo_build (when (not DEMO_BUILD)))\n"
                    "   (store_button (text \"Store\") (select {ui goto_screen store_screen})))") !=
        std::string::npos);
    int sets = 0, order = 0;
    CHECK(syntax::check_conversion(path, *text, c.text, &sets, &order).empty());
    CHECK(sets == 4);
}

TEST_CASE("a region inside a tagged array becomes a block wrapped in its tags") {
    const char* const text =
        "(renderer\n"
        "   (fog 1)\n"
        "#ifdef HANDHELD\n"
        "   (shadows 0)\n"
        "#else\n"
        "   (shadows 1)\n"
        "#endif\n"
        ")\n";
    const syntax::Conversion c = syntax::to_blocks(text);
    CHECK(kinds(c) == "converted");
    CHECK(c.text ==
          "(renderer\n"
          "   (fog 1)\n"
          "   (shadows 1)\n"
          ")\n"
          "\n"
          "(#layer (handheld (when HANDHELD))\n"
          "   (renderer\n"
          "      (shadows 0)))\n");
    CHECK(differs(text).empty());
    // Two regions under one define are one block; #ifndef is (not N); a comment that ends
    // a branch keeps the bracket off its line.
    const char* const two =
        "(audio\n#ifdef HANDHELD\n   (channels 16) ; fewer voices\n#else\n   (channels 32)\n"
        "#endif\n)\n"
        "#ifdef HANDHELD\n(renderer (lod 2))\n#endif\n"
        "#ifndef DEMO\n(store (open TRUE))\n#endif\n";
    const syntax::Conversion d = syntax::to_blocks(two);
    CHECK(d.blocks == std::vector<std::string>{"handheld", "not_demo"});
    CHECK(d.text.find("(audio\n   (channels 32)\n)\n") == 0);
    CHECK(d.text.find("(#layer (handheld (when HANDHELD))\n"
                      "   (audio\n"
                      "      (channels 16) ; fewer voices\n"
                      "      )\n"
                      "   (renderer (lod 2)))\n") != std::string::npos);
    CHECK(differs(two).empty());
}

TEST_CASE("an #else whose keys the defined branch lacks becomes a block of its own") {
    const char* const text =
        "(menu\n#ifdef HAS_VOCALS\n   (vocals (lanes 3))\n#else\n   (guitar (lanes "
        "5))\n#endif\n)\n";
    const syntax::Conversion c = syntax::to_blocks(text);
    CHECK(c.blocks == std::vector<std::string>{"has_vocals", "not_has_vocals"});
    CHECK(c.text.find("(menu\n)\n") == 0);
    int order = 0;
    CHECK(differs(text, &order).empty());
}

TEST_CASE("regions a block cannot express are left as they are, and named") {
    const char* const text =
        "#ifdef A\n(a (b 1)\n#else\n(a (b 2)\n#endif\n   (c 3))\n"  // spans a bracket
        "#ifdef B\n#include other.dta\n#endif\n"                    // holds a directive
        "#ifdef C\n#ifdef D\n(d 1)\n#endif\n#endif\n"               // nested
        "(colors\n#ifdef E\n   red\n#endif\n   blue)\n"             // values
        "(rules {f\n#ifdef F\n   (x 1)\n#endif\n})\n"               // a command
        "(\n#ifdef G\n(g 1)\n#endif\n)\n";                          // an untagged array
    const syntax::Conversion c = syntax::to_blocks(text);
    CHECK(kinds(c) ==
          "spans a bracket, holds a directive, holds a directive, nested in another region, "
          "holds values, not tagged forms, inside an untagged array, command or property, "
          "inside an untagged array, command or property");
    CHECK(c.blocks.empty());
    CHECK(c.text == text);
}

TEST_CASE("the check catches a region whose place decided it, which a block would win") {
    // In the file, (a 2) after the region wins; as a block, (a 1) applies above the file.
    const auto d = differs("#ifdef X\n(a 1)\n#endif\n(a 2)\n");
    REQUIRE(d.size() == 1);
    CHECK(d[0].rfind("{X}: ", 0) == 0);
    // In one array the first of a repeated key is the one read, so a region after the
    // default never took effect; as a block it does.
    const auto e = differs("(audio (channels 32)\n#ifdef HANDHELD\n(channels 16)\n#endif\n)\n");
    REQUIRE(e.size() == 1);
    CHECK(e[0] ==
          "{HANDHELD}: 0: 3 nodes vs 2: (audio (channels 32) (channels 16))  vs  "
          "(audio (channels 16))");
}

TEST_CASE("blocks back to #ifdef: every condition spelled, the merge unchanged") {
    const char* const text =
        "(a 1) (b 1) (c 1) (d 1)\n"
        "(#layer (x (when (and A (not B)))) (a 2))\n"
        "(#layer (y (when (or A B))) (b 2))\n"
        "(#layer z (c 2))\n"
        "(renderer (#layer (w (when (not (or A (and B C))))) (d 2)))\n";
    LoadSession session;
    auto root = read_string(text, "t.dta", session);
    REQUIRE(root);
    const std::string dta = syntax::to_ifdefs(**root, session.blocks());
    CHECK(dta.find("(#layer") == std::string::npos);
    CHECK(dta.find("#ifdef A\n#ifndef B\n(a 2)\n#endif\n#endif\n") != std::string::npos);
    CHECK(syntax::check_conversion("mem/t.dta", dta, text).empty());
    // A disabled block: under a define no host sets, so off, as the block is.
    LoadSession s2;
    auto r2 = read_string("(a 1)\n(#layer (slow (disabled)) (a 0))\n", "t.dta", s2);
    REQUIRE(r2);
    CHECK(syntax::to_ifdefs(**r2, s2.blocks()).find("#ifdef LAYER_SLOW\n(a 0)\n#endif\n") !=
          std::string::npos);
}

TEST_CASE("the defines a file's conditions name") {
    CHECK(syntax::condition_defines("#ifdef A\n#endif\n#ifndef B\n#endif\n"
                                    "(#layer (x (when (and C (not (or D E))))) (a 1))") ==
          std::vector<std::string>{"A", "B", "C", "D", "E"});
}

TEST_CASE("a block's error is located") {
    auto r = syntax::read("(a 1)\n(#layer (x (group arms))\n   (a 2))\n", "broken.dta");
    REQUIRE(!r);
    CHECK(r.error().file().str() == "broken.dta");
    CHECK(r.error().line() == 2);
    CHECK(r.error().column() == 1);
}

TEST_CASE("the samples merge as their originals did") {
    const std::string root(repository_root());
    const std::string samples = root + "/experiments/syntax-inline-layers/samples/";
    auto text = [](const std::string& p) {
        auto t = read_text_file(p);
        REQUIRE(t);
        return *t;
    };
    // examples/config: default.dta and handheld.dta, pushed by the host on a handheld, are
    // one file whose block is on when HANDHELD is defined.
    {
        Config files({.edit_mode = false, .tweaks = nullptr});
        REQUIRE(files.push_layer("default", samples + "config/default.dta"));
        REQUIRE(files.push_layer("handheld", samples + "config/handheld.dta"));
        auto one = syntax::merged(samples + "config/default.dta",
                                  text(samples + "config/default.blocks.dta"), {"HANDHELD"});
        REQUIRE(one);
        CHECK(syntax::compare_merged(*files.root(), **one).empty());
        auto off = syntax::merged(samples + "config/default.dta",
                                  text(samples + "config/default.blocks.dta"), {});
        auto base = syntax::merged(samples + "config/default.dta",
                                   text(samples + "config/default.dta"), {});
        REQUIRE((off && base));
        CHECK(syntax::compare_merged(**base, **off).empty());
    }
    // ui_menu: under every define set.
    CHECK(syntax::check_conversion(root + "/tests/corpus/ui_menu.dta",
                                   text(samples + "ui_menu/ui_menu.dta"),
                                   text(samples + "ui_menu/ui_menu.blocks.dta"))
              .empty());
    // Duel's A/B: the arm files above default.dta, against the blocks; read where Duel's
    // file is, so its #require is found. Then the other arm, toggled both ways.
    const std::string duel = root + "/stacks/duel/data/default.dta";
    for (int arm = 0; arm < 2; ++arm) {
        Config files({.edit_mode = false, .tweaks = nullptr});
        files.session().set_pack_levels({"engine", "game", "task", "live"});
        REQUIRE(files.push_layer("default", duel));
        REQUIRE(files.push_layer("heavy", samples + "duel-ab/hit-pause-heavy.dta"));
        REQUIRE(
            files.push_layer("light", samples + "duel-ab/hit-pause-light.dta", {.enabled = false}));
        Config one({.edit_mode = false, .tweaks = nullptr});
        one.session().set_pack_levels({"engine", "game", "task", "live"});
        const std::string blocks = text(samples + "duel-ab/default.blocks.dta");
        one.session().set_reader([&](std::string_view p, TextSink out) {
            auto t = read_text_file(p);
            if (!t) return false;
            out(std::string_view(p).ends_with("data/default.dta") ? blocks : *t);
            return true;
        });
        REQUIRE(one.push_layer("default", duel));
        CHECK(strings(one.layers()) ==
              std::vector<std::string>{"default", "default:hit_pause_heavy",
                                       "default:hit_pause_light", "scratch"});
        if (arm == 1) {
            REQUIRE(files.toggle_layer("heavy"));
            REQUIRE(files.toggle_layer("light"));
            REQUIRE(one.toggle_layer("default:hit_pause_heavy"));
            REQUIRE(one.toggle_layer("default:hit_pause_light"));
        }
        CHECK(one.get<int>("duel.hit_pause") == (arm ? 4 : 9));
        CHECK(syntax::compare_merged(*files.root(), *one.root()).empty());
    }
}
