#include "experiments.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace baker::experiments {

std::string_view repository_root() {
    return BAKER_EXPERIMENTS_ROOT;
}

std::vector<std::string> corpus_files() {
    static constexpr std::string_view kDirs[] = {"tests/corpus", "tests/shape", "tests/host",
                                                 "examples", "stacks"};
    std::vector<std::string> out;
    const fs::path root(repository_root());
    for (std::string_view d : kDirs) {
        const fs::path dir = root / d;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::path& p = it->path();
            const std::string name = p.filename().string();
            if (it->is_directory() &&
                (name == "runs" || name == "local" || name.rfind("build", 0) == 0)) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && p.extension() == ".dta")
                out.push_back(fs::relative(p, root).generic_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

Expected<std::string> read_text_file(std::string_view path) {
    std::ifstream in((std::string(path)), std::ios::binary);
    if (!in)
        return Unexpected(make_error(ErrorCode::Io, "cannot read '" + std::string(path) + "'"));
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

Expected<Ref<Array>> read_dta_file(std::string_view path) {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    return read_file(path, session);
}

namespace {

void diff_at(const Node& a, const Node& b, std::string& path, std::string& out) {
    if (!out.empty()) return;
    const std::string at = path.empty() ? "root" : path;
    if (a.type() != b.type()) {
        out = at + ": " + to_string(a) + "  vs  " + to_string(b);
        return;
    }
    if (!a.is_array_like()) {
        if (!structurally_equal(a, b)) out = at + ": " + to_string(a) + "  vs  " + to_string(b);
        return;
    }
    const Array& x = *a.array();
    const Array& y = *b.array();
    if (x.size() != y.size()) {
        out = at + ": " + std::to_string(x.size()) + " nodes vs " + std::to_string(y.size()) +
              ": " + to_string(a) + "  vs  " + to_string(b);
        return;
    }
    for (uint32_t i = 0; i < x.size() && out.empty(); ++i) {
        const size_t mark = path.size();
        if (!path.empty()) path += '.';
        path += std::to_string(i);
        diff_at(x[i], y[i], path, out);
        path.resize(mark);
    }
}

}  // namespace

std::string first_difference(const Array& a, const Array& b) {
    if (structurally_equal(a, b)) return {};
    std::string path, out;
    if (a.size() != b.size())
        return "root: " + std::to_string(a.size()) + " nodes vs " + std::to_string(b.size());
    for (uint32_t i = 0; i < a.size() && out.empty(); ++i) {
        path = std::to_string(i);
        diff_at(a[i], b[i], path, out);
    }
    return out.empty() ? std::string("the trees differ") : out;
}

std::vector<RoundTrip> round_trip_corpus(const Printer& to_new, const Reader& from_new,
                                         const std::vector<std::string>& skip) {
    std::vector<RoundTrip> failures;
    for (const std::string& rel : corpus_files()) {
        if (std::find(skip.begin(), skip.end(), rel) != skip.end()) continue;
        const std::string path = std::string(repository_root()) + "/" + rel;
        auto original = read_dta_file(path);
        if (!original) {
            failures.push_back(
                {rel, RoundTrip::Stage::Baker, std::string(to_string(original.error()))});
            continue;
        }
        const std::string text = to_new(**original);
        if (text.empty() && (*original)->size() > 0) {
            failures.push_back({rel, RoundTrip::Stage::Print, "the printer returned no text"});
            continue;
        }
        auto back = from_new(text, rel);
        if (!back) {
            failures.push_back({rel, RoundTrip::Stage::Read, std::string(to_string(back.error()))});
            continue;
        }
        if (std::string diff = first_difference(**original, **back); !diff.empty())
            failures.push_back({rel, RoundTrip::Stage::Compare, diff});
    }
    return failures;
}

std::vector<RoundTrip> experiment_faults(const std::vector<RoundTrip>& failures) {
    std::vector<RoundTrip> out;
    for (const RoundTrip& f : failures)
        if (f.stage != RoundTrip::Stage::Baker) out.push_back(f);
    return out;
}

void print_failures(const std::vector<RoundTrip>& failures) {
    static constexpr const char* kStage[] = {"baker  ", "print  ", "read   ", "compare"};
    for (const RoundTrip& f : failures)
        std::printf("%s  %s: %s\n", kStage[static_cast<int>(f.stage)], f.file.c_str(),
                    f.problem.c_str());
}

std::string generated_config_text(size_t bytes) {
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

BenchRow bench_reader(std::string_view name, const Reader& read, std::string_view text,
                      std::string_view file_name, int runs) {
    double best = 1e9;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        auto r = read(text, file_name);
        const auto t1 = std::chrono::steady_clock::now();
        if (!r) {
            std::fprintf(stderr, "bench: %.*s failed: %.*s\n", static_cast<int>(name.size()),
                         name.data(), static_cast<int>(to_string(r.error()).size()),
                         to_string(r.error()).data());
            return {std::string(name), text.size(), 0, 0};
        }
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    }
    return {std::string(name), text.size(), best * 1e3,
            static_cast<double>(text.size()) / best / 1e6};
}

std::pair<BenchRow, BenchRow> compare_parsers(const Reader& read, std::string_view dta_text,
                                              std::string_view converted_text, int rounds) {
    const Reader baker_read = [](std::string_view text, std::string_view name) {
        return read_string(text, name);
    };
    BenchRow old_best{"baker::read_string", dta_text.size(), 1e9, 0};
    BenchRow new_best{"experiment", converted_text.size(), 1e9, 0};
    for (int r = 0; r < rounds; ++r) {
        const BenchRow o = bench_reader(old_best.name, baker_read, dta_text, "bench.dta", 3);
        const BenchRow n = bench_reader(new_best.name, read, converted_text, "bench.new", 3);
        if (o.mb_per_s == 0 || n.mb_per_s == 0) return {o, n};
        if (o.best_ms < old_best.best_ms) old_best = o;
        if (n.best_ms < new_best.best_ms) new_best = n;
    }
    std::printf("%-20s %10s %10s %10s\n", "reader", "bytes", "best ms", "MB/s");
    for (const BenchRow* row : {&old_best, &new_best})
        std::printf("%-20s %10zu %10.3f %10.1f\n", row->name.c_str(), row->bytes, row->best_ms,
                    row->mb_per_s);
    std::printf("time ratio (experiment / baker, same content): %.3f\n",
                new_best.best_ms / old_best.best_ms);
    return {old_best, new_best};
}

}  // namespace baker::experiments
