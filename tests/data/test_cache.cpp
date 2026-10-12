// The binary cache (D70): a cache load gives the tree the text load gave, as one block.
#include <doctest.h>
#include <moira/cache.hpp>
#include <moira/parse.hpp>
#include <moira/std_interop.hpp>

#include "alloc_counter.hpp"
#include "ok.hpp"
#include "temp_dir.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace moira;
using moira::test::ok;
namespace fs = std::filesystem;

namespace {

std::string bytes_of(const Array& root) {
    std::string out;
    ok(write_cache(root, string_out(out)));
    return out;
}

// Structurally equal, and the same in everything else a reader sees: sources, heights,
// lookup order, and which arrays and strings are shared.
void check_same(const Array& a, const Array& b, std::map<const void*, const void*>& seen) {
    auto [it, added] = seen.emplace(&a, &b);
    REQUIRE(it->second == &b);
    if (!added) return;
    REQUIRE(a.size() == b.size());
    CHECK(a.file() == b.file());
    CHECK(a.line() == b.line());
    CHECK(a.height() == b.height());
    CHECK(a.sorted_for_lookup() == b.sorted_for_lookup());
    for (uint32_t i = 0; i < a.size(); ++i) {
        const Node &x = a[i], &y = b[i];
        // The tag run came through (D77): each tag finds the same child in both.
        if (const auto tag = x.try_array() && !x.try_array()->empty()
                                 ? x.try_array()->nodes()[0].try_sym()
                                 : std::nullopt) {
            const Array* in_a = a.try_find(*tag);
            const Array* in_b = b.try_find(*tag);
            REQUIRE((in_a && in_b));
            auto at = [](const Array& in, const Array* found) {
                uint32_t k = 0;
                while (in[k].array() != found) ++k;
                return k;
            };
            CHECK(at(a, in_a) == at(b, in_b));
        }
        REQUIRE(x.type() == y.type());
        if (x.array()) {
            check_same(*x.array(), *y.array(), seen);
        } else if (x.string()) {
            auto [s, fresh] = seen.emplace(x.string(), y.string());
            CHECK(s->second == y.string());
            CHECK(x.string()->view() == y.string()->view());
            CHECK(y.string()->data()[y.string()->size()] == '\0');
        } else {
            CHECK(structurally_equal(x, y));
        }
    }
}

Ref<Array> round_trip(const Array& root) {
    const std::string bytes = bytes_of(root);
    auto back = ok(read_cache_bytes(bytes, "test.datb"));
    std::map<const void*, const void*> seen;
    check_same(root, *back, seen);
    CHECK(structurally_equal(root, *back));
    return back;
}

std::string big_text(size_t bytes) {
    std::string text;
    for (int id = 0; text.size() < bytes; ++id) {
        const std::string n = std::to_string(id);
        text += "(entry_" + n + " (name \"Entry number " + n + "\") (enabled TRUE) (position " + n +
                ".5 -12.25 0.0) (tags fast loud blue) (limits (min 0) (max 100))" +
                " (on_hit {play_sfx \"hit.wav\"} {game add_score $score}))\n";
    }
    return text;
}

}  // namespace

TEST_CASE("every corpus file loads from a cache as it does from text") {
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(MOIRA_CORPUS_DIR))
        if (e.path().extension() == ".dta") files.push_back(e.path());
    REQUIRE(files.size() >= 8);
    for (const fs::path& p : files) {
        CAPTURE(p.string());
        LoadSession session;
        auto root = ok(read_file(p.generic_string(), session));
        (void)round_trip(*root);
    }
}

TEST_CASE("a cache keeps every kind of value, and what the tree shares") {
    auto root = ok(read_string(R"(
        [SHARED (one 1) (two 2.5)]
        (a SHARED) (b SHARED)
        (values -7 0x7FFFFFFFFFFFFFFF -0.0 1e300 '' 'odd sym' "" "text" $var [prop] {cmd 1})
        (empty ())
        (nested (((deep))))
    )",
                               "kinds.dta"));
    auto back = round_trip(*root);
    // The macro's arrays are shared by both expansions, in the cache as in the text load.
    CHECK((*back->try_find("a"))[1].array() == (*back->try_find("b"))[1].array());

    SUBCASE("a sorted array stays sorted") {
        auto sorted = ok(read_string("(z 1) (a 2) (m 3)", "sorted.dta"));
        sorted->sort_for_lookup();
        auto again = round_trip(*sorted);
        CHECK(again->sorted_for_lookup());
        CHECK(*again->find_int("m") == 3);
    }
}

TEST_CASE("a cache-loaded tree is one block") {
    const std::string text = big_text(200000);
    LoadSession session;
    auto root = ok(read_string(text, "big.dta", session));
    const std::string bytes = bytes_of(*root);
    (void)ok(read_cache_bytes(bytes, "warm.datb", session));  // interns the symbols
    if (test::counting_allocations()) {
        test::AllocationScope scope;
        auto back = ok(read_cache_bytes(bytes, "big.datb", session));
        CHECK(scope.count() == 1);
        test::AllocationScope teardown;
        back.reset();
        CHECK(teardown.count() == 0);
    }
    // Any reference keeps the whole block alive, as in a text load (D64).
    auto back = ok(read_cache_bytes(bytes, "big.datb"));
    Ref<Array> inner(const_cast<Array*>(back->try_get("entry_7.limits.max")));
    const uint32_t refs = back->ref_count();
    back.reset();
    CHECK(inner->ref_count() == refs - 1);
    CHECK((*inner)[1].try_int() == 100);
}

TEST_CASE("a cache-loaded tree can be changed and freed") {
    auto root = ok(read_string("(a 1) (b \"two\")", "set.dta"));
    auto back = round_trip(*root);
    auto other = ok(read_string("(x \"elsewhere\")", "other.dta"));
    auto* a = const_cast<Array*>(back->try_find("a"));
    a->set(1, (*other)[0]);  // depends on the other load now
    other.reset();
    CHECK(to_string(*back) == "((a (x \"elsewhere\")) (b \"two\"))");
}

TEST_CASE("writing refuses what a parse never makes") {
    auto root = Array::make(2, "made.dta", 3);
    root->set(0, Node(Symbol("handle")));
    root->set(1, Node::object(ObjectHandle::make(1, 1)));
    Ref<Array> outer = Array::make(1);
    outer->set(0, Node(root));
    std::string out;
    auto r = write_cache(*outer, string_out(out));
    REQUIRE_FALSE(r);
    CHECK(r.error().code() == ErrorCode::Refused);
    CHECK(to_string(r.error()).find("made.dta:3") != std::string::npos);
    CHECK(out.empty());
}

TEST_CASE("a damaged or foreign cache is an error, never a crash") {
    auto root = ok(read_string("(a 1 \"s\" sym) (b (c))", "small.dta"));
    const std::string good = bytes_of(*root);
    auto fails = [](const std::string& bytes, ErrorCode code, const char* says) {
        auto r = read_cache_bytes(bytes, "bad.datb");
        REQUIRE_FALSE(r);
        CHECK(r.error().code() == code);
        CHECK_MESSAGE(to_string(r.error()).find(says) != std::string::npos, to_string(r.error()));
    };
    fails("", ErrorCode::Io, "not a Moira cache");
    fails("(a 1)", ErrorCode::Io, "not a Moira cache");
    fails(good.substr(0, good.size() - 8), ErrorCode::Io, "damaged");
    for (size_t at : {good.size() / 2, good.size() - 1, size_t{80}}) {
        std::string flipped = good;
        flipped[at] = static_cast<char>(flipped[at] ^ 0x10);
        fails(flipped, ErrorCode::Io, "damaged");
    }
    std::string version = good;
    version[10] = 99;
    fails(version, ErrorCode::Refused, "format version 99");
    std::string endian = good;
    std::swap(endian[12], endian[15]);
    fails(endian, ErrorCode::Refused, "endianness");
}

TEST_CASE("caches on disk, and through a session's reader") {
    test::TempDir dir;
    const std::string text = "(menu (bacon (price 12.75)))";
    auto root = ok(read_string(text, "menu.dta"));
    const std::string path = dir.path("menu.datb");
    ok(write_cache(*root, path));

    LoadSession session;
    auto back = ok(read_cache(path, session));
    CHECK(*back->get<double>("menu.bacon.price") == 12.75);
    CHECK(back->file() == Symbol("menu.dta"));

    // A custom reader is used as for text.
    const std::string bytes = dir.read("menu.datb");
    LoadSession custom;
    custom.set_reader([&](std::string_view, TextSink out) {
        out(bytes);
        return true;
    });
    auto served = ok(read_cache("anywhere.datb", custom));
    CHECK(structurally_equal(*served, *root));

    auto missing = read_cache(dir.path("missing.datb"));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code() == ErrorCode::Io);
    auto unwritable = write_cache(*root, dir.path("no/such/dir/x.datb"));
    REQUIRE_FALSE(unwritable);
    CHECK(unwritable.error().code() == ErrorCode::Io);
}

TEST_CASE("read_cached uses a fresh cache and rewrites a stale one") {
    test::TempDir dir;
    const std::string main =
        dir.write("main.dta", "#include inc.dta\n#include_opt local.dta\n(a 1)");
    dir.write("inc.dta", "(b 2)");
    const std::string cache = dir.path("main.datb");

    // Baked, as moira_bake does: read_cached writes no cache in shipping builds.
    LoadSession first;
    first.record_for_cache = true;
    auto root = ok(read_file(main, first));
    ok(write_cache(*root, cache, &first));
    CHECK(to_string(*root) == "((b 2) (a 1))");
    LoadSession reuse;
    CHECK(structurally_equal(*root, *ok(read_cached(main, cache, reuse))));
    LoadSession again;
    CHECK(cache_fresh(cache, again));

    if constexpr (!kShipping) {
        SUBCASE("an included file changes") {
            dir.write("inc.dta", "(b 3)");
            CHECK_FALSE(cache_fresh(cache, again));
            CHECK(to_string(*ok(read_cached(main, cache, again))) == "((b 3) (a 1))");
            LoadSession third;
            CHECK(cache_fresh(cache, third));
        }
        SUBCASE("an optional file that was missing appears") {
            dir.write("local.dta", "(c 4)");
            CHECK_FALSE(cache_fresh(cache, again));
            CHECK(to_string(*ok(read_cached(main, cache, again))) == "((b 2) (c 4) (a 1))");
        }
        SUBCASE("the session's defines differ") {
            LoadSession debug;
            debug.define("DEBUG");
            CHECK_FALSE(cache_fresh(cache, debug));
        }
    } else {
        SUBCASE("shipping trusts the cache") {
            dir.write("inc.dta", "(b 3)");
            CHECK(to_string(*ok(read_cached(main, cache, again))) == "((b 2) (a 1))");
        }
    }

    // A cache written without a recorded load cannot say whether it is fresh.
    ok(write_cache(*root, dir.path("bare.datb")));
    CHECK_FALSE(cache_fresh(dir.path("bare.datb"), again));
}

TEST_CASE("a cache load leaves the session as the text load did") {
    test::TempDir dir;
    const std::string main = dir.write("main.dta", R"(
        #define FLAG
        #define BODY (x 1)
        #undef GONE
        #ifdef PRE (pre yes) #endif
        #autorun {print "hello"}
        (a BODY)
    )");
    const std::string cache = dir.path("main.datb");
    auto fresh_session = [] {
        LoadSession s;
        s.define("PRE");
        s.define("GONE");
        return s;
    };

    LoadSession text = fresh_session();
    text.record_for_cache = true;
    auto from_text = ok(read_file(main, text));
    ok(write_cache(*from_text, cache, &text));

    LoadSession cached = fresh_session();
    CHECK(cache_fresh(cache, cached));
    auto from_cache = ok(read_cache(cache, cached));
    CHECK(structurally_equal(*from_text, *from_cache));
    for (const char* name : {"FLAG", "BODY", "GONE", "PRE"}) {
        CAPTURE(name);
        CHECK(cached.is_defined(name) == text.is_defined(name));
    }
    REQUIRE(cached.macro("BODY"));
    CHECK(structurally_equal(*cached.macro("BODY"), *text.macro("BODY")));
    REQUIRE(cached.autorun().size() == 1);
    CHECK(to_string(cached.autorun()[0]) == "{print \"hello\"}");
    // The replayed macro keeps the cache's tree alive after the tree is dropped.
    from_cache.reset();
    CHECK(to_string(*cached.macro("BODY")) == "(x 1)");
}

TEST_CASE("a Cipher enciphers what it writes and deciphers what it reads") {
    // Not a cipher anyone should ship: the hook is what matters.
    Cipher x{[](void*, std::span<std::byte> b) {
                 for (std::byte& c : b) c ^= std::byte{0x5A};
             },
             [](void*, std::span<std::byte> b) {
                 for (std::byte& c : b) c ^= std::byte{0x5A};
             },
             nullptr};
    auto root = ok(read_string("(secret \"cheat codes\")", "secret.dta"));
    LoadSession with;
    with.cipher = &x;
    std::string bytes;
    ok(write_cache(*root, string_out(bytes), &with));
    CHECK(bytes.find("cheat codes") == std::string::npos);
    CHECK(structurally_equal(*root, *ok(read_cache_bytes(bytes, "secret.datb", with))));

    auto without = read_cache_bytes(bytes, "secret.datb");
    REQUIRE_FALSE(without);
    CHECK(without.error().code() == ErrorCode::Refused);
    CHECK(to_string(without.error()).find("Cipher") != std::string::npos);
}

TEST_CASE("a cache records the files #require read, and which it found already read") {
    test::TempDir dir;
    dir.write("pack.dta", "#define SPEED (3)\n(pack_read)");
    const std::string main = dir.write("main.dta", "#require pack.dta\n(a SPEED)");
    const std::string cache = dir.path("main.datb");
    LoadSession text;
    text.record_for_cache = true;
    ok(write_cache(*ok(read_file(main, text)), cache, &text));

    // Read from the cache, the pack counts as required, as after the text load.
    LoadSession cached;
    auto tree = ok(read_cache(cache, cached));
    CHECK(to_string(*tree) == "((pack_read) (a 3))");
    CHECK(to_string(*ok(read_string("#require pack.dta\n(b SPEED)", dir.path("x.dta"), cached))) ==
          "((b 3))");
    // A session that has already required the pack would splice nothing: the cache is stale
    // for it.
    CHECK_FALSE(cache_fresh(cache, cached));
    LoadSession clean;
    CHECK(cache_fresh(cache, clean));
}
