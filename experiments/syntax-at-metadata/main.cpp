// The experiment's command line:
//   to-new <file.dta>       read DTA, print the at-metadata form (the migration over the tree)
//   to-dta <file>           read the form, strip the sigils, print DTA
//   check                   round-trip every corpus file; exit 1 on an experiment fault
//   bench [<file.dta>]      the experiment's reader against baker::read_string, same content
//   bench-bind [N]          N tweaks (1,000) bound to a layer with metadata on every value
//   verify <orig> <new>     whether <new> (migrated text) reads as <orig> once stripped
//   count                   the corpus's arrays tagged with an '@'-led symbol, per file
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/tweak.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace baker;
using namespace baker::experiments;

int fail(const Error& e) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(describe(e).size()), describe(e).data());
    return 1;
}

int usage() {
    std::fprintf(stderr,
                 "usage: baker_syntax_%.*s to-new <file.dta> | to-dta <file> | check | bench "
                 "[<file.dta>] | bench-bind [N] | verify <orig.dta> <new.dta> | count\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
}

// A layer of `n` float values, each with a range and a description, spelled for this build.
std::string tweak_layer(int n) {
    const char* range = kAtMetadata ? "(@range 0 100)" : "(range 0 100)";
    const char* desc = kAtMetadata ? "(@desc \"a value\")" : "(desc \"a value\")";
    std::string text = "(tweaks\n";
    for (int i = 0; i < n; ++i)
        text += "   (t" + std::to_string(i) + " " + std::to_string(i % 100) + ".5 " + range + " " +
                desc + ")\n";
    return text + ")\n";
}

// N tweaks bound to a layer whose every value carries metadata: the bind stage of a layer
// push, best of seven, from the Config's own reload record (ReloadRecord::bind_ns), and the
// whole push beside it.
int bench_bind(int n) {
    TweakRegistry reg;
    std::vector<std::unique_ptr<Tweak<float>>> tweaks;
    tweaks.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        tweaks.push_back(std::make_unique<Tweak<float>>(Symbol("tweaks.t" + std::to_string(i)),
                                                        0.0f, TweakMeta{}, reg));
    auto layer = read_string(tweak_layer(n), "tweaks.dta");
    if (!layer) return fail(layer.error());
    double best_bind = 1e30, best_push = 1e30;
    for (int rep = 0; rep < 7; ++rep) {
        Config cfg({.tweaks = &reg});
        const auto t0 = std::chrono::steady_clock::now();
        if (auto r = cfg.push_layer("default", *layer); !r) return fail(r.error());
        const auto t1 = std::chrono::steady_clock::now();
        best_push = std::min(best_push, std::chrono::duration<double>(t1 - t0).count() * 1e3);
        if (const auto rec = cfg.last_reload())
            best_bind = std::min(best_bind, static_cast<double>(rec->bind_ns) / 1e6);
    }
    std::printf("bind of %d tweaks (%s): %.3f ms bind stage, %.3f ms push_layer (best of 7)\n", n,
                kAtMetadata ? "@metadata" : "trailing metadata", best_bind, best_push);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "";
    if (cmd == "to-new" && argc > 2) {
        auto root = read_dta_file(argv[2]);
        if (!root) return fail(root.error());
        std::fputs(syntax::print(**root).c_str(), stdout);
        return 0;
    }
    if (cmd == "to-dta" && argc > 2) {
        auto text = read_text_file(argv[2]);
        if (!text) return fail(text.error());
        auto root = syntax::read_as_dta(*text, argv[2]);
        if (!root) return fail(root.error());
        std::fputs(to_dta(**root, {.indent = 3, .width = 96}).c_str(), stdout);
        return 0;
    }
    if (cmd == "check") {
        const auto failures = round_trip_corpus(syntax::print, syntax::read_as_dta);
        print_failures(failures);
        const auto faults = experiment_faults(failures);
        std::printf("%zu corpus files, %zu failures, %zu the experiment's\n", corpus_files().size(),
                    failures.size(), faults.size());
        return faults.empty() ? 0 : 1;
    }
    if (cmd == "bench") {
        std::string text;
        if (argc > 2) {
            auto t = read_text_file(argv[2]);
            if (!t) return fail(t.error());
            text = *t;
        } else {
            text = generated_config_text(4 * 1024 * 1024);
        }
        auto root = read_string(text, "bench.dta");
        if (!root) return fail(root.error());
        const std::string converted = syntax::print(**root);
        // The form's own reader (read_structure's placement check, then the parser), and the
        // parser alone on the converted text, which is what the core would pay.
        compare_parsers(syntax::read, text, converted);
        const Reader parser = [](std::string_view t, std::string_view name) {
            return read_string(t, name);
        };
        std::printf("the parser alone on the converted text:\n");
        compare_parsers(parser, text, converted);
        return 0;
    }
    if (cmd == "bench-bind") return bench_bind(argc > 2 ? std::atoi(argv[2]) : 1000);
    if (cmd == "verify" && argc > 3) {
        auto original = read_dta_file(argv[2]);
        if (!original) return fail(original.error());
        auto text = read_text_file(argv[3]);
        if (!text) return fail(text.error());
        auto migrated = syntax::read_as_dta(*text, argv[3]);
        if (!migrated) return fail(migrated.error());
        const std::string diff = first_difference(**original, **migrated);
        if (!diff.empty()) {
            std::printf("%s: differs at %s\n", argv[3], diff.c_str());
            return 1;
        }
        auto as_read = syntax::read(*text, argv[3]);
        std::printf("%s: reads as %s once stripped; %zu metadata tags\n", argv[3], argv[2],
                    syntax::count_meta_tags(**as_read));
        return 0;
    }
    if (cmd == "count") {
        size_t total = 0, files = 0;
        for (const std::string& rel : corpus_files()) {
            auto root = read_dta_file(std::string(repository_root()) + "/" + rel);
            if (!root) continue;
            const size_t n = syntax::count_meta_tags(**root);
            if (n) std::printf("%s: %zu\n", rel.c_str(), n);
            total += n;
            ++files;
        }
        std::printf("%zu files read, %zu arrays tagged with an '@'-led symbol\n", files, total);
        return 0;
    }
    return usage();
}
