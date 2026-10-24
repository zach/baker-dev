#include "syntax.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <functional>
#include <set>

namespace baker::experiments::syntax {
namespace {

const Symbol& sym(int which) {
    static const Symbol s[] = {Symbol("#append"), Symbol("#drop"), Symbol("#was"),
                               Symbol("#remove")};
    return s[which];
}
const Symbol& kAppend() {
    return sym(0);
}
const Symbol& kDrop() {
    return sym(1);
}
const Symbol& kWas() {
    return sym(2);
}
const Symbol& kRemove() {
    return sym(3);
}

bool tagged(const Node& n) {
    return n.type() == Type::Array && !n.array()->tag().empty();
}

Ref<Array> ref(const Array& a) {
    return Ref<Array>(const_cast<Array*>(&a));
}

Ref<Array> make(std::vector<Node> nodes, const Array& at) {
    return Array::make_from(nodes, at.file(), at.line());
}

bool same(std::span<const Node> a, std::span<const Node> b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!structurally_equal(a[i], b[i])) return false;
    return true;
}

bool composes(const Array& a) {
    return a.size() > 1 && !a.tag().empty() && (a[1].is(kAppend()) || a[1].is(kDrop()));
}

// `rows` (one key's) over `base_row`, the row the merge reads.
Ref<Array> merged_row(const Array& base_row, const std::vector<Ref<Array>>& rows) {
    std::vector<Node> b{Node(ref(base_row))}, c;
    for (const Ref<Array>& r : rows) c.emplace_back(r);
    const Ref<Array> merged =
        merge(*Array::make_from(b), *Array::make_from(c), MergePolicy::Overwrite);
    const Array* row = merged->try_find(base_row.tag());
    return row ? ref(*row) : Ref<Array>();
}

using Rows = std::vector<Ref<Array>>;

// `a` rewritten against `b`, the base's row with its tag: the rows that say it (none when it
// changes nothing). What `a` means: a row with values, or one that restates most of the
// base's nodes, says its whole run (a list); any other says its keyed children (a record).
Rows convert(const Array& a, const Array* b) {
    if (!b) return {ref(a)};
    if (structurally_equal(a, *b)) return {};
    const auto ln = a.nodes().subspan(1), bn = b->nodes().subspan(1);
    bool values = false;
    for (const Node& n : ln) values = values || !tagged(n);
    size_t kept = 0;
    for (const Node& n : bn)
        for (const Node& m : ln)
            if (structurally_equal(n, m)) {
                ++kept;
                break;
            }
    const bool list = values || (!bn.empty() && 2 * kept >= bn.size());
    const Ref<Array> meant = list ? ref(a) : merged_row(*b, {ref(a)});
    // A tagged child against the base's child of its tag: converted, left out, or as is.
    auto child = [&](const Node& n, std::vector<Node>& out) {
        const Array* bc = tagged(n) ? b->try_find(n.array()->tag()) : nullptr;
        if (!bc) {
            out.push_back(n);
            return;
        }
        for (Ref<Array>& k : convert(*n.array(), bc)) out.emplace_back(std::move(k));
    };
    auto verb = [&](const Symbol& v, std::span<const Node> operands) {
        std::vector<Node> row{a[0], Node(v)};
        row.insert(row.end(), operands.begin(), operands.end());
        return make(row, a);
    };
    std::vector<Rows> candidates;
    if (!values) {  // a record: keyed children say only what changed
        std::vector<Node> kids{a[0]};
        for (const Node& n : ln) child(n, kids);
        if (kids.size() > 1 && !list) {
            // Each child is converted to what it means; where the base has each child's tag
            // once, that is what the record means.
            bool once = true;
            for (const Node& n : ln) {
                size_t count = 0;
                for (const Node& m : bn)
                    count += tagged(n) && tagged(m) && m.array()->tag() == n.array()->tag();
                once = once && count <= 1;
            }
            if (once) return {make(kids, a)};
        }
        if (kids.size() > 1) candidates.push_back({make(kids, a)});
    }
    // The longest head of the row that is the base's run with nodes taken out; the rest of
    // the row is added after them.
    std::vector<Node> removed;
    size_t j = 0;
    for (const Node& n : bn) {
        if (j < ln.size() && structurally_equal(n, ln[j]))
            ++j;
        else
            removed.push_back(n);
    }
    const auto added = ln.subspan(j);
    // A run of numbers is a vector (a position, a color): its values are pinned, never
    // appended to or dropped from. Symbols, strings and tuples are a list's elements.
    bool vector = false;
    for (const Node& n : bn) vector = vector || n.type() == Type::Int || n.type() == Type::Float;
    if (!vector && removed.empty() && !added.empty())
        candidates.push_back({verb(kAppend(), added)});
    if (!vector && !removed.empty() && added.empty())
        candidates.push_back({verb(kDrop(), removed)});
    if (!vector && !removed.empty() && !added.empty() && j > 0)  // two rows of one key, in order
        candidates.push_back({verb(kDrop(), removed), verb(kAppend(), added)});
    if (values) {  // a value row, pinned to the base's values
        std::vector<Node> row{a[0]}, base_values, kids;
        for (const Node& n : ln)
            if (tagged(n))
                child(n, kids);
            else
                row.push_back(n);
        for (const Node& n : bn)
            if (!tagged(n)) base_values.push_back(n);
        if (!base_values.empty()) {
            row.emplace_back(kWas());
            row.insert(row.end(), base_values.begin(), base_values.end());
        }
        row.insert(row.end(), kids.begin(), kids.end());
        candidates.push_back({make(row, a)});
    }
    for (const Rows& c : candidates) {
        const Ref<Array> got = merged_row(*b, c);
        if (got && meant && structurally_equal(*got, *meant)) return c;
    }
    return {ref(a)};
}

// One line of DTA, without its newline.
std::string line_of(const Array& root) {
    std::string s = to_dta(root, {.indent = 0, .width = 1 << 20});
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// (t1 (t2 ... (key run...))) as a layer's root.
Ref<Array> nest(const std::vector<Symbol>& path, std::vector<Node> run) {
    run.insert(run.begin(), Node(path.back()));
    Ref<Array> a = Array::make_from(run);
    for (size_t i = path.size() - 1; i-- > 0;) {
        std::vector<Node> outer{Node(path[i]), Node(a)};
        a = Array::make_from(outer);
    }
    std::vector<Node> root{Node(a)};
    return Array::make_from(root);
}

const Array* at(const Array& root, const std::vector<Symbol>& path) {
    const Array* a = &root;
    for (const Symbol s : path)
        if (!(a = a->try_find(s))) return nullptr;
    return a;
}

}  // namespace

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    return read_string(text, name);
}

std::string print(const Array& root) {
    return to_dta(root, {.indent = 3, .width = 96});
}

Ref<Array> to_markers(const Array& base, const Array& layer) {
    std::vector<Node> out;
    for (const Node& n : layer) {
        if (!tagged(n)) {
            out.push_back(n);
            continue;
        }
        for (Ref<Array>& k : convert(*n.array(), base.try_find(n.array()->tag())))
            out.emplace_back(std::move(k));
    }
    return make(out, layer);
}

Ref<Array> resolve(const Array& base, const Array& layer) {
    const Ref<Array> merged = merge(base, layer, MergePolicy::Overwrite);
    std::set<const Array*> restated;  // a key's verb rows become one restated row
    std::function<Ref<Array>(const Array&, const Array*)> walk = [&](const Array& a,
                                                                     const Array* m) {
        if (composes(a))  // the list as merged; none for a #drop of a key the base lacks
            return m && restated.insert(m).second ? ref(*m) : Ref<Array>();
        std::vector<Node> out;
        bool pinned = false;
        for (size_t i = 0; i < a.size(); ++i) {
            const Node& n = a[i];
            if (i > 0 && n.is(kWas())) {
                pinned = true;
                continue;
            }
            if (pinned && !tagged(n)) continue;
            if (i > 0 && tagged(n)) {
                const Array& c = *n.array();
                if (Ref<Array> r = walk(c, m ? m->try_find(c.tag()) : nullptr))
                    out.emplace_back(std::move(r));
                continue;
            }
            out.push_back(n);
        }
        return make(out, a);
    };
    std::vector<Node> out;
    for (const Node& n : layer) {
        if (!tagged(n)) {
            out.push_back(n);
            continue;
        }
        if (Ref<Array> r = walk(*n.array(), merged->try_find(n.array()->tag())))
            out.emplace_back(std::move(r));
    }
    return make(out, layer);
}

bool marker_free(const Array& layer) {
    for (const Node& n : layer) {
        if (n.is(kAppend()) || n.is(kDrop()) || n.is(kWas())) return false;
        if (n.type() == Type::Array && !marker_free(*n.array())) return false;
    }
    return true;
}

std::vector<ListRow> list_rows(const Array& root) {
    static const std::set<std::string_view> names = {"cancels",   "items", "things", "blocks",
                                                     "climbable", "drops", "glides", "objectives"};
    std::vector<ListRow> out;
    std::vector<Symbol> path;
    std::function<void(const Array&)> walk = [&](const Array& a) {
        for (const Node& n : a) {
            if (!tagged(n)) continue;
            const Array& c = *n.array();
            path.push_back(c.tag());
            if (names.count(c.tag().str()) && c.size() > 1 && path[0] != Symbol("shapes")) {
                ListRow r;
                r.file = c.file().str();
                for (const Symbol s : path)
                    r.path += (r.path.empty() ? "" : ".") + std::string(s.str());
                r.line = c.line();
                const auto run = c.nodes().subspan(1);
                r.elements = run.size();
                std::set<std::string_view> tags;
                bool all_tagged = true;
                for (const Node& e : run) {
                    all_tagged = all_tagged && tagged(e);
                    if (tagged(e)) tags.insert(e.array()->tag().str());
                }
                // One value that holds the elements, as (glides ((x y z) (x y z))); a single
                // tuple, as (blocks (x z w d h)), is a list of one.
                r.one_value = run.size() == 1 && run[0].type() == Type::Array && !tagged(run[0]) &&
                              run[0].array()->size() > 0 &&
                              (*run[0].array())[0].type() == Type::Array;
                r.kind = r.one_value                 ? ListKind::OneValue
                         : !all_tagged               ? ListKind::Values
                         : tags.size() == run.size() ? ListKind::Keyed
                                                     : ListKind::KeyedRepeats;
                // The element a package adds: a new symbol, a copy of the last tuple, or a
                // keyed thing of a new kind.
                Node added = Node(Symbol("new_item"));
                if (r.kind == ListKind::Values && run.back().type() == Type::Array)
                    added = run.back();
                if (r.kind == ListKind::Keyed) {
                    std::vector<Node> t{Node(Symbol("added")), Node(int64_t{1})};
                    added = Node(Array::make_from(t));
                }
                if (r.kind == ListKind::KeyedRepeats)  // one more of a kind it has twice
                    for (size_t i = 0; i < run.size(); ++i)
                        for (size_t k = i + 1; k < run.size(); ++k)
                            if (run[k].array()->tag() == run[i].array()->tag()) added = run[k];
                std::vector<Node> grown(run.begin(), run.end()), shrunk(run.begin() + 1, run.end());
                grown.push_back(added);
                if (r.one_value) {  // the elements are inside the one value
                    const auto inner = run[0].array()->nodes();
                    std::vector<Node> g(inner.begin(), inner.end()),
                        d(inner.begin() + 1, inner.end());
                    g.push_back(inner.back());
                    grown = {Node(Array::make_from(g))};
                    shrunk = {Node(Array::make_from(d))};
                }
                const Ref<Array> add_restated = nest(path, grown);
                const Ref<Array> add_keyed = nest(path, {added});
                const Ref<Array> add_verb = nest(path, {Node(kAppend()), added});
                const Ref<Array> drop_restated = nest(path, shrunk);
                const Ref<Array> drop_verb = nest(path, {Node(kDrop()), run[0]});
                const bool keyed = r.kind == ListKind::Keyed;
                std::vector<Node> remove_first;
                if (keyed) remove_first = {Node(run[0].array()->tag()), Node(kRemove())};
                const Ref<Array> drop_keyed =
                    keyed ? nest(path, {Node(Array::make_from(remove_first))}) : nullptr;
                const Ref<Array> add_today = keyed ? add_keyed : add_restated;
                const Ref<Array> drop_today = keyed ? drop_keyed : drop_restated;
                r.add_before = line_of(*add_today).size();
                r.add_after = line_of(*add_verb).size();
                r.drop_before = line_of(*drop_today).size();
                r.drop_after = line_of(*drop_verb).size();
                // What each reads over the file, against what the package meant.
                auto reads = [&](const Ref<Array>& layer, const std::vector<Node>& meant) {
                    const Array* got = at(*merge(root, *layer, MergePolicy::Overwrite), path);
                    return got && same(got->nodes().subspan(1), meant);
                };
                r.add_same = !r.one_value && reads(add_verb, grown);
                r.add_today_works = reads(add_today, grown);
                r.drop_same = !r.one_value && reads(drop_verb, shrunk);
                r.drop_today_works = reads(drop_today, shrunk);
                if (r.one_value) r.add_after = r.drop_after = 0;  // no verb reaches inside
                out.push_back(std::move(r));
            }
            walk(c);
            path.pop_back();
        }
    };
    walk(root);
    return out;
}

}  // namespace baker::experiments::syntax
