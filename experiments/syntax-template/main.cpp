// The experiment's command line:
//   to-new <file.dta>     read DTA, print the experiment's form
//   to-dta <file>         read the experiment's form, print DTA
//   check                 round-trip every corpus file; exit 1 on an experiment fault
//   bench [<file.dta>]    the experiment's reader against baker::read_string, same content
#include "experiments.hpp"
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <cstdio>
#include <string>
#include <string_view>

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
                 "[<file.dta>]\n",
                 static_cast<int>(syntax::kName.size()), syntax::kName.data());
    return 2;
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
        auto root = syntax::read(*text, argv[2]);
        if (!root) return fail(root.error());
        std::fputs(to_dta(**root, {.indent = 3, .width = 96}).c_str(), stdout);
        return 0;
    }
    if (cmd == "check") {
        const auto failures = round_trip_corpus(syntax::print, syntax::read);
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
        compare_parsers(syntax::read, text, converted);
        return 0;
    }
    return usage();
}
