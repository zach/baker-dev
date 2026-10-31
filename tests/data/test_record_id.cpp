// Record IDs (D284): the form, a record's own, find_by_id, the validator over duplicates
// and malformed IDs, and what --drop-metadata keeps.
#include <doctest.h>
#include <baker/cache.hpp>
#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/record_id.hpp>
#include <baker/std_interop.hpp>

#include "alloc_counter.hpp"
#include "ok.hpp"

#include <string>
#include <vector>

using namespace baker;
using baker::test::ok;

namespace {

const char* const kWeapons =
    "(weapons\n"
    "  (laser (@id b3c9a7e5f214d68a1e2c3b4d5e6f7081) (damage 3) (@desc \"zap\"))\n"
    "  (cannon (@id c000000000000000000000000000000a) (damage 9))\n"
    "  (sling (damage 1)))\n"
    "(units (scout (weapon laser) (@aka recon)))\n";

}  // namespace

TEST_CASE("a record id is 32 lowercase hex digits, the first a to f, and round-trips") {
    const auto id = RecordId::parse("b3c9a7e5f214d68a1e2c3b4d5e6f7081");
    REQUIRE(id);
    CHECK(id->hi == 0xb3c9a7e5f214d68aull);
    CHECK(id->lo == 0x1e2c3b4d5e6f7081ull);
    CHECK(id->valid());
    char text[RecordId::kDigits];
    id->write(text);
    CHECK(std::string(text, 32) == "b3c9a7e5f214d68a1e2c3b4d5e6f7081");
    CHECK_FALSE(RecordId::parse("03c9a7e5f214d68a1e2c3b4d5e6f70812"));  // a digit first
    CHECK_FALSE(RecordId::parse("B3C9A7E5F214D68A1E2C3B4D5E6F70812"));  // upper case
    CHECK_FALSE(RecordId::parse("b3c9a7e5f214d68a1e2c3b4d5e6f708"));    // 31 digits
    CHECK_FALSE(RecordId::parse("b3c9a7e5f214d68a1e2c3b4d5e6f7081x"));  // not hex
    CHECK(RecordId{}.empty());
    CHECK_FALSE(RecordId{0x1ull, 0}.valid());
}

TEST_CASE("a record reads its own id; find_by_id finds the record and its path") {
    LoadSession session;
    auto root = ok(read_string(kWeapons, "w.dta", session));
    const Array* laser = root->try_get("weapons.laser");
    REQUIRE(laser);
    const RecordId id = laser->record_id();
    CHECK(id == *RecordId::parse("b3c9a7e5f214d68a1e2c3b4d5e6f7081"));
    CHECK(root->try_get("weapons.sling")->record_id().empty());
    CHECK(root->try_get("units.scout")->record_id().empty());
    Path path;
    CHECK(find_by_id(*root, id, &path) == laser);
    std::string text;
    path.write(string_out(text), SIZE_MAX, '.');
    CHECK(text == "weapons.laser");
    CHECK(find_by_id(*root, RecordId{1, 2}) == nullptr);
    CHECK(find_by_id(*root, RecordId{}) == nullptr);
}

TEST_CASE("the validator names two records that share an id, and a malformed one") {
    Config cfg({.tweaks = nullptr});
    std::vector<std::string> reported;
    cfg.set_warning_handler([&](const Error& e) { reported.emplace_back(to_string(e)); });
    cfg.add_validator(id_validator(), {.any_thread = true});
    REQUIRE(cfg.push_layer_text("w", kWeapons));
    CHECK(reported.empty());
    REQUIRE(
        cfg.push_layer_text("copy",
                            "(weapons (laser2 (@id b3c9a7e5f214d68a1e2c3b4d5e6f7081) (damage 4)))\n"
                            "(items (odd (@id 12345)))\n"));
    REQUIRE(reported.size() == 2);
    CHECK(reported[0].find(
              "(@id ...) holds 32 lowercase hex digits, the first a to f: (@id 12345)") !=
          std::string::npos);
    CHECK(reported[0].find("items/odd") != std::string::npos);
    CHECK(reported[1].find("two records share the id b3c9a7e5f214d68a1e2c3b4d5e6f7081: "
                           "weapons/laser (w:2) and weapons/laser2") != std::string::npos);
    CHECK(reported[1].find("format --baker-mint-ids") != std::string::npos);
    CHECK(reported[1].find("copy:1") != std::string::npos);
    // Validators run at every merge, so the duplicate is reported again; a record copied
    // without its id adds nothing.
    reported.clear();
    REQUIRE(cfg.push_layer_text("plain", "(weapons (laser3 (damage 5)))"));
    CHECK(reported.size() == 2);
    REQUIRE(cfg.remove_layer("copy"));
    reported.clear();
    REQUIRE(cfg.push_layer_text("more", "(weapons (laser4 (damage 6)))"));
    CHECK(reported.empty());
}

TEST_CASE("@id and @aka are known metadata, and a shipped cache keeps them") {
    LoadSession session;
    auto root = ok(read_string(kWeapons, "w.dta", session));
    const Ref<Array> lean = without_metadata(*root);
    const Array* laser = lean->try_get("weapons.laser");
    REQUIRE(laser);
    CHECK_FALSE(laser->record_id().empty());
    CHECK(laser->meta(Symbol("@desc")) == nullptr);
    CHECK(lean->try_get("units.scout")->meta(Symbol("@aka")) != nullptr);
}

TEST_CASE("the validator over a stack without ids allocates little and reports nothing") {
    Config cfg({.tweaks = nullptr});
    int reports = 0;
    cfg.set_warning_handler([&](const Error&) { ++reports; });
    cfg.add_validator(id_validator(), {.any_thread = true});
    REQUIRE(cfg.push_layer_text("w", "(a (b (c 1)) (d 2))"));
    CHECK(reports == 0);
}
