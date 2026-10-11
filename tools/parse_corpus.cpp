// Parses every .dta file under the given paths and reports which ones fail.
//
//   moira_parse_corpus [--roundtrip] [-D NAME]... [-I DIR]... PATH...
//
// Use it to check the parser against a local checkout of a real DTA corpus (for example
// mariteaux/milo-script-library). Nothing from such a corpus is part of this repository;
// see docs/decisions.md D3. Each file is read with a fresh session; includes resolve
// against the file's own directory, then each -I directory, then the PATH it was found
// under. Exits 1 if any file fails.

#include <moira/parse.hpp>
#include <moira/std_interop.hpp>
#include <moira/write.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool fits(const moira::Node& n, uint64_t& budget) {
    if (budget == 0) return false;
    --budget;
    if (const moira::Array* a = n.array())
        for (const moira::Node& c : *a)
            if (!fits(c, budget)) return false;
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    bool roundtrip = false;
    std::vector<std::string> defines, include_dirs, roots;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--roundtrip") {
            roundtrip = true;
        } else if (arg == "-D" && i + 1 < argc) {
            defines.emplace_back(argv[++i]);
        } else if (arg == "-I" && i + 1 < argc) {
            include_dirs.emplace_back(argv[++i]);
        } else if (arg == "-h" || arg == "--help" || arg[0] == '-') {
            std::fprintf(stderr, "usage: %s [--roundtrip] [-D NAME]... [-I DIR]... PATH...\n",
                         argv[0]);
            return 2;
        } else {
            roots.push_back(arg);
        }
    }
    if (roots.empty()) {
        std::fprintf(stderr, "usage: %s [--roundtrip] [-D NAME]... [-I DIR]... PATH...\n", argv[0]);
        return 2;
    }

    struct Item {
        fs::path file;
        fs::path root;
    };
    std::vector<Item> items;
    for (const std::string& r : roots) {
        std::error_code ec;
        if (fs::is_regular_file(r, ec)) {
            items.push_back({r, fs::path(r).parent_path()});
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(r, ec); !ec && it != fs::end(it);
             it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".dta")
                items.push_back({it->path(), r});
        }
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.file < b.file; });

    size_t ok = 0, failed = 0, roundtrip_failed = 0;
    for (const Item& item : items) {
        moira::LoadSession session;
        for (const std::string& d : include_dirs) session.add_include_dir(d);
        session.add_include_dir(item.root.generic_string());
        for (const std::string& d : defines) session.define(moira::Symbol(d));
        auto root = moira::read_file(item.file.generic_string(), session);
        if (!root) {
            ++failed;
            std::printf("FAIL %s\n     %s\n", item.file.generic_string().c_str(),
                        moira::to_string(root.error()).data());
            continue;
        }
        ++ok;
        if (!roundtrip) continue;
        uint64_t budget = 10'000'000;
        bool small = true;
        for (const moira::Node& n : **root) small = small && fits(n, budget);
        if (!small) continue;
        auto again = moira::read_string(moira::to_dta(**root), "printed.dta");
        if (!again || !moira::structurally_equal(**root, **again)) {
            ++roundtrip_failed;
            std::printf("ROUNDTRIP %s\n", item.file.generic_string().c_str());
        }
    }
    const size_t total = ok + failed;
    std::printf("%zu/%zu files parsed (%.2f%%)", ok, total,
                total ? 100.0 * static_cast<double>(ok) / static_cast<double>(total) : 100.0);
    if (roundtrip) std::printf(", %zu round-trip failures", roundtrip_failed);
    std::printf("\n");
    return failed == 0 && roundtrip_failed == 0 ? 0 : 1;
}
