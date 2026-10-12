// The binary cache (D70). Target (spec §7): a 5 MB tree loads in <= 10 ms with <= 2
// allocations. A cache is the tree as it lies in memory, about 4.3 bytes per byte of
// bench_parse's text, so 1.2 MB of text is the 5 MB tree; 4 MB is bench_parse's own size.
// Rows: the text parse beside it, writing the cache, and the load from a file and from
// memory, with the symbols already interned (warm) and in a fresh process (cold); and
// development's read_cached, which re-hashes the text to know the cache is fresh.
#include <moira/cache.hpp>
#include <moira/parse.hpp>

#include "alloc_counter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// bench_parse's text, to `bytes`.
std::string make_text(size_t bytes) {
    std::string text;
    int id = 0;
    while (text.size() < bytes) {
        const std::string n = std::to_string(id++);
        text += "; entry " + n + "\n(entry_" + n + "\n   (name \"Entry number " + n +
                "\")\n"
                "   (enabled TRUE)\n   (position " +
                n +
                ".5 -12.25 0.0)\n"
                "   (color 0.2 0.4 0.8 1.0)\n   (tags fast loud blue)\n"
                "   (limits (min 0) (max 100) (step 0.5))\n"
                "   (on_hit {play_sfx \"hit.wav\"} {game add_score 10}))\n";
    }
    return text;
}

// One load in this process, before anything else interns a symbol.
int cold(const char* path) {
    moira::LoadSession session;
    const auto t0 = Clock::now();
    auto r = moira::read_cache(path, session);
    const double ms = ms_since(t0);
    if (!r) return 1;
    std::printf("  cold symbols (fresh process): %.2f ms\n", ms);
    return 0;
}

int run(const char* self, size_t text_bytes) {
    const std::string text = make_text(text_bytes);
    double parse = 1e9;
    moira::Ref<moira::Array> tree;
    for (int rep = 0; rep < 5; ++rep) {
        const auto t0 = Clock::now();
        auto r = moira::read_string(text, "bench.dta");
        parse = std::min(parse, ms_since(t0));
        if (!r) return 1;
        tree = std::move(*r);
    }

    std::string bytes;
    double write = 1e9;
    for (int rep = 0; rep < 5; ++rep) {
        bytes.clear();
        const auto t0 = Clock::now();
        if (!moira::write_cache(*tree, moira::TextSink{[](void* u, std::string_view t) {
                                                           static_cast<std::string*>(u)->append(t);
                                                       },
                                                       &bytes}))
            return 1;
        write = std::min(write, ms_since(t0));
    }
    const std::string path =
        (std::filesystem::temp_directory_path() / "moira_bench_cache.datb").string();
    if (!moira::write_cache(*tree, path)) return 1;

    moira::LoadSession session;
    double from_file = 1e9, from_memory = 1e9, free_ms = 1e9;
    size_t file_allocations = 0, memory_allocations = 0;
    for (int rep = 0; rep < 10; ++rep) {
        moira::test::AllocationScope scope;
        auto t0 = Clock::now();
        auto r = moira::read_cache(path, session);
        from_file = std::min(from_file, ms_since(t0));
        file_allocations = scope.count();
        if (!r || !moira::structurally_equal(*tree, **r)) return 1;
        t0 = Clock::now();
        r = moira::Expected<moira::Ref<moira::Array>>();
        free_ms = std::min(free_ms, ms_since(t0));

        moira::test::AllocationScope memory;
        t0 = Clock::now();
        auto m = moira::read_cache_bytes(bytes, "bench.datb", session);
        from_memory = std::min(from_memory, ms_since(t0));
        memory_allocations = memory.count();
        if (!m) return 1;
    }

    std::printf("%.1f MB of text, a %.1f MB tree\n", static_cast<double>(text.size()) / 1e6,
                static_cast<double>(bytes.size()) / 1e6);
    std::printf("  text parse:      %7.2f ms (%.1f MB/s)\n", parse,
                static_cast<double>(text.size()) / 1e3 / parse);
    std::printf("  write_cache:     %7.2f ms\n", write);
    std::printf("  read_cache file: %7.2f ms, %zu allocations (warm symbols)\n", from_file,
                file_allocations);
    std::printf("  from memory:     %7.2f ms, %zu allocations (warm symbols)\n", from_memory,
                memory_allocations);
    std::printf("  freeing it:      %7.3f ms\n", free_ms);

    // Development's path: the text on disk, read through read_cached.
    const std::string text_path =
        (std::filesystem::temp_directory_path() / "moira_bench_cache.dta").string();
    if (std::FILE* f = std::fopen(text_path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    std::filesystem::remove(path);
    double miss = 0, hit = 1e9, check = 1e9;
    {
        moira::LoadSession s;
        const auto t0 = Clock::now();
        if (!moira::read_cached(text_path, path, s)) return 1;
        miss = ms_since(t0);
    }
    for (int rep = 0; rep < 5; ++rep) {
        moira::LoadSession s;
        auto t0 = Clock::now();
        if (!moira::cache_fresh(path, s)) return 1;
        check = std::min(check, ms_since(t0));
        t0 = Clock::now();
        if (!moira::read_cached(text_path, path, s)) return 1;
        hit = std::min(hit, ms_since(t0));
    }
    std::filesystem::remove(text_path);
    std::printf("  read_cached, no cache (parse, write): %7.2f ms\n", miss);
    std::printf("  read_cached, fresh (load, re-hash):   %7.2f ms\n", hit);
    std::printf("  cache_fresh:                          %7.2f ms\n", check);
    std::fflush(stdout);
    // One sample per process, so three of them: one can land on a busy moment.
    int r = 0;
    for (int rep = 0; rep < 3 && r == 0; ++rep)
        r = std::system(("\"" + std::string(self) + "\" --cold \"" + path + "\"").c_str());
    std::filesystem::remove(path);
    return r == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--cold") return cold(argv[2]);
    for (const size_t kb : {1200, 4096})
        if (run(argv[0], kb * 1024) != 0) return 1;
    return 0;
}
