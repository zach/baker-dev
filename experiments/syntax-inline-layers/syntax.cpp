#include "syntax.hpp"

#include "experiments.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <utility>

namespace fs = std::filesystem;

namespace baker::experiments::syntax {

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    return read_string(text, name);
}

std::string print(const Array& root) {
    return to_dta(root, {.indent = 3, .width = 96});
}

// --- Blocks back to #ifdef ---

namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// `cond` spelled with #ifdef, #ifndef and #else: `then` where it holds, `other` where not.
// (and A B) nests; (or A B) is A, else B; (not C) swaps the branches. A branch may repeat.
std::string emit(const Node& cond, const std::string& then, const std::string& other) {
    if (const auto s = cond.try_sym()) {
        const std::string name(s->str());
        if (then.empty()) return "#ifndef " + name + "\n" + other + "#endif\n";
        return "#ifdef " + name + "\n" + then + (other.empty() ? "" : "#else\n" + other) +
               "#endif\n";
    }
    const Array* a = cond.try_array();
    const std::string op = a && !a->tag().empty() ? std::string(a->tag().str()) : "";
    if (op == "not" && a->size() == 2) return emit((*a)[1], other, then);
    if ((op == "and" || op == "or") && a->size() >= 2) {
        if (a->size() == 2) return emit((*a)[1], then, other);
        std::vector<Node> rest{Node(Symbol(op))};
        for (size_t i = 2; i < a->size(); ++i) rest.push_back((*a)[i]);
        const Node tail(Array::make_from(rest));
        return op == "and" ? emit((*a)[1], emit(tail, then, other), other)
                           : emit((*a)[1], then, emit(tail, then, other));
    }
    return "; a condition #ifdef cannot spell: " + to_string(cond) + "\n";
}

}  // namespace

std::string to_ifdefs(const Array& root, std::span<const Block> blocks) {
    std::string out = print(root);
    for (const Block& b : blocks) {
        const std::string name(b.name.str());
        std::string forms = print(*b.tree);
        if (!forms.empty() && forms.back() != '\n') forms += '\n';
        out += "\n; the layer block '" + name + "' (" + std::string(b.file.str()) + ":" +
               std::to_string(b.line) + "), which applies above the whole file\n";
        if (b.disabled) {
            const Node flag{Symbol("LAYER_" + upper(name))};
            if (b.when.type() == Type::Int) {
                out += emit(flag, forms, "");
            } else {
                std::vector<Node> both{Node(Symbol("and")), flag, b.when};
                out += emit(Node(Array::make_from(both)), forms, "");
            }
        } else if (b.when.type() == Type::Int) {
            out += forms;
        } else {
            out += emit(b.when, forms, "");
        }
    }
    return out;
}

// --- Merged trees ---

Expected<Ref<Array>> merged(std::string_view path, std::string_view text,
                            const std::vector<std::string>& defines) {
    Config cfg({.edit_mode = false, .tweaks = nullptr});
    cfg.set_warning_handler([](const Error&) {});
    LoadSession& s = cfg.session();
    s.set_pack_levels({"engine", "game", "task", "live"});
    for (const std::string& d : defines) s.define(Symbol(d));
    const fs::path self = fs::path(path).lexically_normal();
    const std::string body(text);
    s.set_reader([self, body](std::string_view p, TextSink out) {
        if (fs::path(p).lexically_normal() == self) {
            out(body);
            return true;
        }
        auto t = read_text_file(p);
        if (t) out(*t);
        return bool(t);
    });
    if (auto r = cfg.push_layer("file", path); !r) return Unexpected(r.error());
    return cfg.root();
}

namespace {

// A tree's text with each array's tagged children in tag order (stably), its values first:
// what a lookup by key sees, whatever order the keys were added in.
std::string canonical(const Node& n) {
    const Array* a = n.type() == Type::Array ? n.try_array() : nullptr;
    if (!a) return to_string(n);
    std::string out = "(";
    std::vector<std::pair<std::string, std::string>> tagged;
    for (size_t i = 0; i < a->size(); ++i) {
        const Node& c = (*a)[i];
        const Array* ca = c.type() == Type::Array ? c.try_array() : nullptr;
        if (i > 0 && ca && !ca->tag().empty())
            tagged.emplace_back(std::string(ca->tag().str()), canonical(c));
        else
            out += canonical(c) + " ";
    }
    std::stable_sort(tagged.begin(), tagged.end(),
                     [](const auto& x, const auto& y) { return x.first < y.first; });
    for (const auto& t : tagged) out += t.second + " ";
    return out + ")";
}

}  // namespace

std::string compare_merged(const Array& a, const Array& b) {
    if (structurally_equal(a, b)) return "";
    // The top-level keys too, in tag order.
    auto sorted = [](const Array& r) {
        std::vector<std::string> v;
        for (const Node& n : r) v.push_back(canonical(n));
        std::stable_sort(v.begin(), v.end());
        return v;
    };
    if (sorted(a) == sorted(b)) return "order";
    const std::string diff = first_difference(a, b);
    return diff.empty() ? "the trees differ" : diff;
}

std::vector<std::string> check_conversion(std::string_view path, std::string_view original,
                                          std::string_view converted, int* sets, int* order_only) {
    std::set<std::string> names;
    for (const std::string& d : condition_defines(original)) names.insert(d);
    for (const std::string& d : condition_defines(converted)) names.insert(d);
    std::vector<std::string> all(names.begin(), names.end());
    if (all.size() > 10) all.resize(10);
    std::vector<std::string> out;
    int count = 0, order = 0;
    for (uint32_t mask = 0; mask < (1u << all.size()); ++mask) {
        std::vector<std::string> defines;
        std::string label = "{";
        for (size_t k = 0; k < all.size(); ++k)
            if (mask & (1u << k)) {
                defines.push_back(all[k]);
                label += (label.size() > 1 ? " " : "") + all[k];
            }
        label += "}";
        ++count;
        auto a = merged(path, original, defines);
        auto b = merged(path, converted, defines);
        if (!a || !b) {
            out.push_back(label + ": " +
                          (a ? "the conversion: " + std::string(describe(b.error()))
                             : "the original: " + std::string(describe(a.error()))));
            continue;
        }
        const std::string d = compare_merged(**a, **b);
        if (d == "order")
            ++order;
        else if (!d.empty())
            out.push_back(label + ": " + d);
    }
    if (sets) *sets = count;
    if (order_only) *order_only = order;
    return out;
}

}  // namespace baker::experiments::syntax
