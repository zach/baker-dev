// Content tables (D269): records designers author, read into structs the build generated
// from their shape. The build ran `baker_bake --views` over data/baker-project.dta
// (examples/CMakeLists.txt, baker_generate_views), which wrote game::Move, its reader and a
// digest of the shape's layout into the build tree.
//
//   baker_example_content_tables data/baker-project.dta
#include <baker/config.hpp>
#include <baker/project.hpp>
#include <baker/shape.hpp>
#include <baker/view_read.hpp>

#include "move_read.hpp"  // generated: the struct and its reader
#include "views.hpp"      // generated: every view, and kViews for the drift check

#include <cstdio>
#include <string_view>

namespace {

void print(std::string_view s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s data/baker-project.dta\n", argv[0]);
        return 2;
    }
    auto project = baker::read_project(argv[1]);
    if (!project) {
        print(baker::to_string(project.error()));
        return 1;
    }
    baker::Config cfg({.tweaks = nullptr});
    if (auto r = baker::open_project(cfg, *project); !r) {
        print(baker::to_string(r.error()));
        return 1;
    }

    // In development: are the compiled views the shapes the stack declares? A designer who
    // changes a field's type sees this before the programmer regenerates.
    {
        baker::shape::Report report;
        const auto roots = baker::shape::contract_roots(cfg, project->views.layers.items());
        const baker::shape::Set contract =
            baker::shape::contract_set(cfg.read_session(), roots.items(), report);
        baker::shape::check_views(contract, game::kViews, report);
        if (report.warnings.empty())
            std::printf("views: %zu, up to date\n", std::size(game::kViews));
        for (const baker::Error& w : report.warnings) {
            print(baker::to_string(w));
            print("\n");
        }
    }

    // One record, by path: every field typed, the absent ones their defaults.
    auto jab = baker::read_view<game::Move>(cfg, "fighters.kestrel.moves.jab");
    if (!jab) {
        print(baker::to_string(jab.error()));
        return 1;
    }
    std::printf("jab: startup %d, damage %.1f, button %s, cancels into", jab->startup, jab->damage,
                jab->button.c_str());
    for (const baker::Symbol into : jab->cancel_into) std::printf(" %s", into.c_str());
    std::printf("\n");

    // The whole table: one pass over each fighter's moves, as a game fills its own arrays
    // at load. The content layer's field is a member; a field marked (@view no) stays in
    // the tree, for the script that runs it.
    const baker::Ref<baker::Array> root = cfg.root();  // held: the views borrow from it
    int moves = 0;
    for (const baker::Node& f : root->try_find("fighters")->nodes().subspan(1)) {
        const baker::Array* fighter = f.try_array();
        const baker::Array* table = fighter ? fighter->try_find("moves") : nullptr;
        if (!table) continue;
        for (const baker::Node& m : table->nodes().subspan(1)) {
            const baker::Array* record = m.try_array();
            if (!record) continue;
            auto move = baker::read_view<game::Move>(*record);
            if (!move) {
                print(baker::to_string(move.error()));
                return 1;
            }
            ++moves;
            if (!move->hint.empty())
                std::printf("%s.%s: \"%.*s\"\n", fighter->tag().c_str(), record->tag().c_str(),
                            static_cast<int>(move->hint.size()), move->hint.data());
            if (record->try_find("on_hit"))
                std::printf("%s.%s runs a command on a hit\n", fighter->tag().c_str(),
                            record->tag().c_str());
        }
    }
    std::printf("%d moves read\n", moves);

    // A value of the wrong type is a located error naming the field, not a wrong struct.
    if (auto r = cfg.push_layer_text("agent", "(fighters (kestrel (moves (jab (startup fast)))))");
        !r) {
        print(baker::to_string(r.error()));
        return 1;
    }
    auto bad = baker::read_view<game::Move>(cfg, "fighters.kestrel.moves.jab");
    if (!bad) {
        print(baker::to_string(bad.error()));
        print("\n");
    }
    return 0;
}
