// Writes the binary cache of a DTA file, for a build step (docs/decisions.md D70, D72).
//
//   moira_bake [-D NAME]... [-I DIR]... INPUT.dta OUTPUT.datb
//
// The cache records the files the load read and the defines given here, so a game's
// read_cached() or cache_fresh() with the same defines finds it fresh. Exits 1 on any
// error, naming it.

#include <moira/cache.hpp>
#include <moira/parse.hpp>
#include <moira/std_interop.hpp>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    moira::LoadSession session;
    session.record_for_cache = true;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-D" || arg == "-I") && i + 1 < argc) {
            if (arg == "-D")
                session.define(argv[++i]);
            else
                session.add_include_dir(argv[++i]);
        } else {
            paths.push_back(arg);
        }
    }
    if (paths.size() != 2) {
        std::fprintf(stderr, "usage: moira_bake [-D NAME]... [-I DIR]... INPUT.dta OUTPUT.datb\n");
        return 2;
    }
    auto root = moira::read_file(paths[0], session);
    if (!root) {
        std::fprintf(stderr, "%s\n", moira::to_string(root.error()).data());
        return 1;
    }
    if (auto r = moira::write_cache(**root, paths[1], &session); !r) {
        std::fprintf(stderr, "%s\n", moira::to_string(r.error()).data());
        return 1;
    }
    return 0;
}
