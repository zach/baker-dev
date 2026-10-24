#include "syntax.hpp"

#include "experiments.hpp"

#include <baker/parse.hpp>
#include <baker/script.hpp>
#include <baker/source_map.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <optional>

namespace baker::experiments::syntax {

namespace {

// An operator as the reader takes one: a symbol of these bytes only, and never `/*`, which
// opens a comment (src/data/text.hpp has the core's own test; a tool keeps its own copy).
bool operator_symbol(std::string_view s) {
    return !s.empty() && s.find_first_not_of("+-*/%<>=!&|^~") == std::string_view::npos &&
           !(s.size() >= 2 && s[0] == '/' && s[1] == '*');
}

// The guard's operand: a number, a variable, a property or a command.
bool operand(const Node& n) {
    switch (n.type()) {
        case Type::Int:
        case Type::Float:
        case Type::Var:
        case Type::Property:
        case Type::Command: return true;
        default: return false;
    }
}

bool any_operand(const Array& a, uint32_t from) {
    for (uint32_t i = from; i < a.size(); ++i)
        if (operand(a[i])) return true;
    return false;
}

// Whether the printer can write this command infix: an operator with two or more operands,
// or an `if` with two or three arguments, and an operand among them.
bool infix_able(const Array& a) {
    if (a.size() < 3 || !any_operand(a, 1)) return false;
    const auto head = a[0].try_sym();
    if (!head) return false;
    if (a[0].is("if")) return a.size() <= 4;
    return operator_symbol(head->str());
}

// A copy of the tree under `n`, each command marked written infix as `mark` says.
template <class Mark>
Node copy(const Node& n, const Mark& mark) {
    if (!n.is_array_like()) return n;
    const Array& a = *n.array();
    std::vector<Node> kids;
    kids.reserve(a.size());
    for (const Node& k : a) kids.push_back(copy(k, mark));
    Ref<Array> out = Array::make_from(kids, a.file(), a.line());
    out->set_written_infix(n.type() == Type::Command && mark(*out));
    return Node(std::move(out), n.type());
}

template <class Mark>
Ref<Array> copy_root(const Array& root, const Mark& mark) {
    std::vector<Node> kids;
    kids.reserve(root.size());
    for (const Node& k : root) kids.push_back(copy(k, mark));
    return Array::make_from(kids, root.file(), root.line());
}

constexpr WriteOptions kLayout{.indent = 3, .width = 96};

// --- The converter ---

// One node as converted: its new tree and text, where the original is written, and its
// children converted (parallel to `node`'s, for an array).
struct Out {
    Node node;
    std::string text;
    uint32_t begin = 0, end = 0;
    bool located = false;  // written in this file, not spliced from an include
    bool macro = false;    // a macro's use: its node as expanded, its text the macro's name
    bool changed = false;
    std::vector<Out> kids;
};

Out synthetic(Node n) {
    Out o;
    o.text = to_string(n);
    o.node = std::move(n);
    return o;
}

bool comparison(std::string_view s) {
    return s == "<" || s == "<=" || s == ">" || s == ">=";
}
bool pure(const Node& n) {
    return n.type() == Type::Var || n.type() == Type::Property;
}

// A comparison read low to high: `{> a b}` is b < a.
struct Ascending {
    const Out* low;
    std::string rel;
    const Out* high;
    bool turned;
};
std::optional<Ascending> ascending(const Out& c) {
    if (c.node.type() != Type::Command || c.kids.size() != 3 || !c.kids[1].located ||
        !c.kids[2].located || c.kids[2].begin < c.kids[1].end)
        return std::nullopt;
    const auto head = c.node.array()->operator[](0).try_sym();
    if (!head || !comparison(head->str())) return std::nullopt;
    const std::string op(head->str());
    if (op[0] == '<') return Ascending{&c.kids[1], op, &c.kids[2], false};
    return Ascending{&c.kids[2], op == ">" ? "<" : "<=", &c.kids[1], true};
}

class Converter {
public:
    Converter(const SourceMap& map, Symbol file, std::string_view src, Converted& out)
        : map_(map), file_(file), src_(src), out_(out) {}

    void root(const Array& root) {
        const auto spans = map_.spans(root);
        if (!spans) {  // rebuilt by #merge: nothing here is rewritten
            out_.text = std::string(src_);
            out_.tree = copy_root(root, [](const Array&) { return false; });
            return;
        }
        Out r;
        r.located = true;
        r.end = static_cast<uint32_t>(src_.size());
        for (uint32_t i = 0; i < root.size(); ++i)
            r.kids.push_back(convert(root[i], &(*spans)[i], false));
        out_.text = splice(r);
        std::vector<Node> nodes;
        for (const Out& k : r.kids) nodes.push_back(k.node);
        out_.tree = Array::make_from(nodes, root.file(), root.line());
    }

private:
    Out convert(const Node& n, const SourceSpan* sp, bool in_command) {
        Out o;
        o.node = n;
        if (!sp || sp->file.empty() || sp->file != file_) return o;
        o.located = true;
        o.begin = sp->begin;
        o.end = sp->end;
        o.text = std::string(src_.substr(o.begin, o.end - o.begin));
        o.macro = !sp->macro.empty();
        if (!n.is_array_like() || o.macro) return o;
        const Array& a = *n.array();
        const auto spans = map_.spans(a);
        if (!spans) return o;
        const bool command = n.type() == Type::Command;
        bool any = false;
        o.kids.reserve(a.size());
        for (uint32_t i = 0; i < a.size(); ++i) {
            o.kids.push_back(convert(a[i], &(*spans)[i], in_command || command));
            any = any || o.kids.back().changed;
        }
        if (!(command && rewrite(o, n, any)) && any) {
            o.node = rebuild(a, n.type(), o.kids, false);
            o.text = splice(o);
            o.changed = true;
        }
        if (command && !in_command) {
            Counts& c = out_.counts;
            const std::string before(src_.substr(o.begin, o.end - o.begin));
            const TextSize b = text_size(before), after = text_size(o.text);
            ++c.top_commands;
            c.all_before.add(b);
            c.all_after.add(after);
            if (o.changed) {
                ++c.top_changed;
                c.changed_before.add(b);
                c.changed_after.add(after);
                out_.examples.push_back({a.line(), before, o.text});
            }
        }
        return o;
    }

    static Node rebuild(const Array& a, Type type, const std::vector<Out>& kids, bool infix) {
        std::vector<Node> nodes;
        nodes.reserve(kids.size());
        for (const Out& k : kids) nodes.push_back(k.node);
        Ref<Array> arr = Array::make_from(nodes, a.file(), a.line());
        arr->set_written_infix(infix);
        return Node(std::move(arr), type);
    }
    static Node command(const Array& like, std::vector<Node> nodes) {
        Ref<Array> arr = Array::make_from(nodes, like.file(), like.line());
        arr->set_written_infix(true);
        return Node(std::move(arr), Type::Command);
    }

    // `o`'s source with each located child's bytes replaced by its new text.
    std::string splice(const Out& o) const {
        std::string s;
        uint32_t at = o.begin;
        for (const Out& k : o.kids) {
            if (!k.located || k.begin < at) continue;  // a macro's nodes, or another file's
            s.append(src_.substr(at, k.begin - at));
            s += k.text;
            at = k.end;
        }
        s.append(src_.substr(at, o.end - at));
        return s;
    }

    // Whether a comment sits in `o` between its brackets and children: rewriting `o` whole
    // would lose it.
    bool has_comment(const Out& o) const {
        uint32_t at = o.begin;
        auto commented = [&](uint32_t from, uint32_t to) {
            const std::string_view g = src_.substr(from, to > from ? to - from : 0);
            return g.find(';') != std::string_view::npos || g.find("/*") != std::string_view::npos;
        };
        for (const Out& k : o.kids) {
            if (!k.located || k.begin < at) continue;
            if (commented(at, k.begin)) return true;
            at = k.end;
        }
        return commented(at, o.end);
    }

    // What goes after an operator before the next operand: a space, or the line break and
    // indentation the source had between the two operands.
    std::string gap(const Out& a, const Out& b) const {
        if (b.begin < a.end) return " ";
        const std::string_view g = src_.substr(a.end, b.begin - a.end);
        if (g.find('\n') == std::string_view::npos) return " ";
        const size_t line = src_.rfind('\n', b.begin - 1) + 1;
        const size_t text = std::min<size_t>(src_.find_first_not_of(" \t", line), b.begin);
        return "\n" + std::string(src_.substr(line, text - line));
    }

    // Whether every child is written in this file; from 1, the operands only (a converted
    // command's head may be one it was given, written nowhere).
    static bool all_located(const Out& o, size_t from = 0) {
        return std::all_of(
            o.kids.begin() + static_cast<std::ptrdiff_t>(std::min(from, o.kids.size())),
            o.kids.end(), [](const Out& k) { return k.located; });
    }

    // Whether each operand is written apart from the one before it: not two nodes of one
    // macro's use, which a rewrite would write twice.
    static bool distinct(const Out& o) {
        for (size_t i = 2; i < o.kids.size(); ++i)
            if (o.kids[i].begin < o.kids[i - 1].end) return false;
        return true;
    }

    // Rewrites a converted command whole, or (a band whose bounds cannot chain) turns its
    // comparisons and leaves the splice to the caller through `any`.
    bool rewrite(Out& o, const Node& original, bool& any) {
        if (o.kids.size() < 3 || !all_located(o) || !distinct(o) || o.kids[0].macro) return false;
        const auto head = o.kids[0].node.try_sym();
        if (!head) return false;
        const std::string_view h = head->str();
        if (h == "set") return assign(o, original);
        if (h == "and") return band(o, original, any);
        if (h == "if") return ternary(o);
        if (operator_symbol(h)) return chain(o, std::string(h));
        return false;
    }

    bool refuse_for_comment(std::initializer_list<const Out*> outs) {
        for (const Out* o : outs)
            if (has_comment(*o)) {
                ++out_.counts.kept_for_comment;
                return true;
            }
        return false;
    }

    // {op a b ...} -> {a op b op ...}; a nest of the same operator merges into it where the
    // fold is the same: anywhere for + and *, leftmost for -.
    bool chain(Out& o, const std::string& op) {
        const Array& a = *o.node.array();
        const bool assoc = op == "+" || op == "*";
        const bool left = assoc || op == "-";  // `/` takes two
        std::vector<const Out*> operands;
        int merged = 0, reassociated = 0;
        for (size_t i = 1; i < o.kids.size(); ++i) {
            const Out& k = o.kids[i];
            const bool nest = k.node.type() == Type::Command && k.kids.size() >= 3 &&
                              k.kids[0].node.is(Symbol(op)) && all_located(k, 1) && distinct(k) &&
                              (assoc || (left && i == 1));
            if (nest && !has_comment(k)) {
                for (size_t j = 1; j < k.kids.size(); ++j) operands.push_back(&k.kids[j]);
                ++merged;
                if (i != 1) ++reassociated;
            } else {
                operands.push_back(&k);
            }
        }
        bool fires = false;
        for (const Out* x : operands) fires = fires || operand(x->node);
        if (!fires) return false;
        if (refuse_for_comment({&o})) return false;
        std::vector<Node> nodes{o.kids[0].node};
        std::string text = "{" + operands[0]->text;
        for (size_t j = 0; j < operands.size(); ++j) {
            nodes.push_back(operands[j]->node);
            if (j > 0) text += " " + op + gap(*operands[j - 1], *operands[j]) + operands[j]->text;
        }
        text += "}";
        std::vector<Out> kids{synthetic(o.kids[0].node)};
        for (const Out* x : operands) kids.push_back(*x);
        if (merged > 0) {
            const Node before = o.node;  // the nest, as converted so far
            o.node = command(a, nodes);
            out_.rewrites.push_back({Rewrite::Kind::Flatten, before, o.node});
        } else {
            o.node = command(a, nodes);
        }
        o.text = std::move(text);
        o.kids = std::move(kids);
        o.changed = true;
        ++out_.counts.chains;
        out_.counts.flattened += merged;
        out_.counts.reassociated += reassociated;
        return true;
    }

    // {and {>= $v lo} {<= $v hi}} -> {lo <= $v <= hi}, when $v is a variable or property and
    // the bounds are not commands (the chain evaluates $v once and the bounds in order). A
    // pair that cannot chain (two relations, a bound that is a command) has each comparison
    // turned to read low to high, `{0 < $v}`, beside `{$v <= hi}`.
    bool band(Out& o, const Node& original, bool& any) {
        if (o.kids.size() != 3) return false;
        const auto x = ascending(o.kids[1]);
        const auto y = ascending(o.kids[2]);
        if (!x || !y) return false;
        const Out *low, *value, *high;
        std::string rel_low, rel_high;
        if (pure(x->high->node) && structurally_equal(x->high->node, y->low->node)) {
            low = x->low, value = x->high, high = y->high, rel_low = x->rel, rel_high = y->rel;
        } else if (pure(x->low->node) && structurally_equal(x->low->node, y->high->node)) {
            low = y->low, value = y->high, high = x->high, rel_low = y->rel, rel_high = x->rel;
        } else {
            return false;
        }
        const Array& a = *o.node.array();
        if (rel_low == rel_high && low->node.type() != Type::Command &&
            high->node.type() != Type::Command) {
            if (refuse_for_comment({&o, &o.kids[1], &o.kids[2]})) return false;
            const Symbol rel(rel_low);
            o.node = command(a, {Node(rel), low->node, value->node, high->node});
            o.text = "{" + low->text + " " + rel_low + " " + value->text + " " + rel_high + " " +
                     high->text + "}";
            o.kids = {synthetic(Node(rel)), *low, *value, *high};
            o.changed = true;
            ++out_.counts.bands;
            out_.rewrites.push_back({Rewrite::Kind::Band, original, o.node});
            return true;
        }
        for (size_t i = 1; i <= 2; ++i) {
            Out& c = o.kids[i];
            const auto asc = ascending(c);
            if (!asc->turned || asc->low->node.type() == Type::Command ||
                asc->high->node.type() == Type::Command || has_comment(c))
                continue;
            const Symbol rel(asc->rel);
            const Node before = c.node;
            Out turned;
            turned.node = command(*c.node.array(), {Node(rel), asc->low->node, asc->high->node});
            turned.text = "{" + asc->low->text + " " + asc->rel + " " + asc->high->text + "}";
            turned.begin = c.begin;
            turned.end = c.end;
            turned.located = true;
            turned.changed = true;
            turned.kids = {synthetic(Node(rel)), *asc->low, *asc->high};
            out_.rewrites.push_back({Rewrite::Kind::Ascending, before, turned.node});
            c = std::move(turned);
            ++out_.counts.ascending;
            any = true;
        }
        return false;
    }

    // {set $x {+ $x n}} -> {$x += n}, for + - * / and a variable or property.
    bool assign(Out& o, const Node& original) {
        if (o.kids.size() != 3 || !pure(o.kids[1].node)) return false;
        const Out& e = o.kids[2];
        if (e.node.type() != Type::Command || e.kids.size() != 3 || !all_located(e, 1) ||
            !distinct(e))
            return false;
        const auto op = e.kids[0].node.try_sym();
        if (!op || (op->str() != "+" && op->str() != "-" && op->str() != "*" && op->str() != "/"))
            return false;
        if (!structurally_equal(e.kids[1].node, o.kids[1].node)) return false;
        if (refuse_for_comment({&o, &e})) return false;
        const std::string name = std::string(op->str()) + "=";
        const Out place = o.kids[1], by = e.kids[2];
        o.node = command(*o.node.array(), {Node(Symbol(name)), place.node, by.node});
        o.text = "{" + place.text + " " + name + " " + by.text + "}";
        o.kids = {synthetic(Node(Symbol(name))), place, by};
        o.changed = true;
        ++out_.counts.assignments;
        out_.rewrites.push_back({Rewrite::Kind::Assignment, original, o.node});
        return true;
    }

    // {if c a b}, written on one line, -> {c ? a : b}: an `if` that picks a value. A
    // two-argument `if`, or one over several lines, is a statement and stays as it is.
    bool ternary(Out& o) {
        if (o.kids.size() != 4) return false;
        if (o.text.find('\n') != std::string::npos) return false;
        if (!operand(o.kids[1].node) && !operand(o.kids[2].node) && !operand(o.kids[3].node))
            return false;
        if (refuse_for_comment({&o})) return false;
        o.node = rebuild(*o.node.array(), Type::Command, o.kids, true);
        o.text = "{" + o.kids[1].text + " ? " + o.kids[2].text + " : " + o.kids[3].text + "}";
        o.changed = true;
        ++out_.counts.ternaries;
        return true;
    }

    const SourceMap& map_;
    Symbol file_;
    std::string_view src_;
    Converted& out_;
};

void mark_difference(const Node& a, const Node& b, std::string& path, std::string& out) {
    if (!out.empty() || !a.is_array_like() || !b.is_array_like()) return;
    const Array& x = *a.array();
    const Array& y = *b.array();
    if (a.type() == Type::Command && x.written_infix() != y.written_infix()) {
        out = (path.empty() ? "root" : path) + ": " + to_string(a) + " marked " +
              (x.written_infix() ? "infix" : "prefix") + ", read back " +
              (y.written_infix() ? "infix" : "prefix");
        return;
    }
    for (uint32_t i = 0; i < x.size() && i < y.size() && out.empty(); ++i) {
        const size_t mark = path.size();
        if (!path.empty()) path += '.';
        path += std::to_string(i);
        mark_difference(x[i], y[i], path, out);
        path.resize(mark);
    }
}

void collect(const Node& n, std::vector<Symbol>& vars, std::vector<double>& numbers) {
    if (n.type() == Type::Var && std::find(vars.begin(), vars.end(), n.var_name()) == vars.end())
        vars.push_back(n.var_name());
    if (const auto i = n.try_int()) numbers.push_back(static_cast<double>(*i));
    if (const auto f = n.try_float()) numbers.push_back(*f);
    if (n.is_array_like())
        for (const Node& k : *n.array()) collect(k, vars, numbers);
}

std::string show(const Expected<Node>& r) {
    return r ? to_string(*r) : "error: " + std::string(r.error().message());
}

// Replaces each subtree of `n` that is a recorded rewrite's result with what it replaced: the
// original's tree back, if the rewrites are all that changed it. `used` counts each kind.
Node undo(const Node& n, const std::vector<Rewrite>& rewrites, std::vector<int>& used) {
    if (!n.is_array_like()) return n;
    for (const Rewrite& w : rewrites)
        if (n.type() == Type::Command && structurally_equal(n, w.after)) {
            ++used[static_cast<size_t>(w.kind)];
            return undo(w.before, rewrites, used);
        }
    const Array& a = *n.array();
    std::vector<Node> kids;
    kids.reserve(a.size());
    for (const Node& k : a) kids.push_back(undo(k, rewrites, used));
    return Node(Array::make_from(kids, a.file(), a.line()), n.type());
}

}  // namespace

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    return read_string(text, name);
}

std::string print(const Array& root) {
    return to_dta(*copy_root(root, infix_able), kLayout);
}

std::string print_prefix(const Array& root) {
    return to_dta(*copy_root(root, [](const Array&) { return false; }), kLayout);
}

void Counts::add(const Counts& o) {
    top_commands += o.top_commands;
    top_changed += o.top_changed;
    chains += o.chains;
    bands += o.bands;
    ascending += o.ascending;
    ternaries += o.ternaries;
    assignments += o.assignments;
    flattened += o.flattened;
    reassociated += o.reassociated;
    kept_for_comment += o.kept_for_comment;
    changed_before.add(o.changed_before);
    changed_after.add(o.changed_after);
    all_before.add(o.all_before);
    all_after.add(o.all_after);
}

Expected<Converted> to_new(std::string_view path) {
    auto text = read_text_file(path);
    if (!text) return Unexpected(std::move(text).error());
    return to_new(path, *text);
}

Expected<Converted> to_new(std::string_view path, std::string_view text) {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    SourceMap map;
    session.source_map = &map;
    auto root = read_string(text, path, session);
    if (!root) return Unexpected(std::move(root).error());
    const SourceSpan* extent = map.extent(**root);
    const auto src = extent ? map.text(extent->file) : std::nullopt;
    if (!src)
        return Unexpected(
            make_error(ErrorCode::Io, "no source recorded for '" + std::string(path) + "'"));
    Converted out;
    Converter(map, extent->file, *src, out).root(**root);
    return out;
}

TextSize text_size(std::string_view t) {
    TextSize s;
    std::string compact;
    size_t i = 0;
    auto bracket = [](char c) {
        return c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']';
    };
    auto opening = [](char c) { return c == '(' || c == '{' || c == '['; };
    while (i < t.size()) {
        const char c = t[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else if (c == ';') {
            while (i < t.size() && t[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
            const size_t e = t.find("*/", i + 2);
            i = e == std::string_view::npos ? t.size() : e + 2;
        } else {
            size_t e = i + 1;
            if (bracket(c)) {
                ++s.brackets;
            } else {
                if (c == '"' || c == '\'') {
                    while (e < t.size() && t[e] != c) e += t[e] == '\\' ? 2 : 1;
                    e = std::min(e + 1, t.size());
                } else {
                    while (e < t.size() && !bracket(t[e]) && t[e] != ' ' && t[e] != '\t' &&
                           t[e] != '\n' && t[e] != '\r' && t[e] != ';')
                        ++e;
                }
                ++s.atoms;
            }
            // Compact text: one space between tokens, none inside a bracket's edge.
            if (!compact.empty() && !opening(compact.back()) && !(bracket(c) && !opening(c)))
                compact += ' ';
            compact.append(t.substr(i, e - i));
            i = e;
        }
    }
    s.chars = static_cast<int>(compact.size());
    return s;
}

// Evaluates `before` and `after` with each variable they read set to values around each
// number they hold.
std::string agree(const Rewrite& w, bool& evaluated) {
    std::vector<Symbol> vars;
    std::vector<double> numbers{0, 1};
    collect(w.before, vars, numbers);
    std::vector<Node> values;
    for (const double x : numbers)
        for (const double d : {-1.0, -1e-9, 0.0, 1e-9, 1.0}) values.emplace_back(x + d);
    for (const double x : numbers)
        if (x == static_cast<double>(static_cast<int64_t>(x)))
            values.emplace_back(static_cast<int64_t>(x));
    evaluated = false;
    const std::vector<Symbol>& names = vars;
    if (names.size() > 2) return {};
    const size_t n2 = names.size() == 2 ? values.size() : 1;
    for (size_t i = 0; i < (names.empty() ? 1 : values.size()); ++i) {
        for (size_t j = 0; j < n2; ++j) {
            Expected<Node> r[2];
            Node stored[2];
            for (int side = 0; side < 2; ++side) {
                Context ctx;
                if (!names.empty()) ctx.var(names[0]) = values[i];
                if (names.size() == 2) ctx.var(names[1]) = values[j];
                r[side] = ctx.eval(side == 0 ? w.before : w.after);
                if (!names.empty()) stored[side] = ctx.var(names[0]).value();
            }
            if (!r[0] &&
                std::string(r[0].error().message()).find("is not a function") != std::string::npos)
                return {};  // a host function: not evaluable here
            evaluated = true;
            const bool same = r[0] && r[1] ? structurally_equal(*r[0], *r[1]) : !r[0] && !r[1];
            if (!same || !structurally_equal(stored[0], stored[1]))
                return to_string(w.before) + " gives " + show(r[0]) + ", " + to_string(w.after) +
                       " gives " + show(r[1]);
        }
    }
    return {};
}

std::span<const Original> originals() {
    static constexpr Original kOriginals[] = {
        {"calculator-functions", "examples/calculator/functions.dta"},
        {"contraption-puzzle", "stacks/contraption/metrics/puzzle.dta"},
        {"cube", "stacks/cube/metrics/cube.dta"},
        {"duel", "stacks/duel/metrics/duel.dta"},
        {"hearth", "stacks/hearth/metrics/hearth.dta"},
        {"host-objectives", "tests/host/stack/metrics/objectives.dta"},
        {"ledger", "stacks/ledger/metrics/ledger.dta"},
        {"ledger-economy", "stacks/ledger/data/economy.dta"},
        {"ledger-milestones", "stacks/ledger/data/default.dta"},
        {"scripting-rules", "examples/scripting/rules.dta"},
        {"scripts", "tests/corpus/scripts.dta"},
        {"stuntline-track", "stacks/stuntline/metrics/track.dta"},
        {"ui_menu", "tests/corpus/ui_menu.dta"},
        {"wayfarer-world", "stacks/wayfarer/metrics/world.dta"},
    };
    return kOriginals;
}

Expected<std::string> original_text(std::string_view rel) {
    const std::string root(repository_root());
    for (const Original& o : originals())
        if (o.path == rel)
            return read_text_file(root + "/experiments/syntax-curly-infix/samples/" +
                                  std::string(o.name) + ".original.dta");
    return read_text_file(root + "/" + std::string(rel));
}

int written_infix(const Array& root, std::string_view file) {
    int c = 0;
    auto count = [&](auto& self, const Node& n) -> void {
        if (!n.is_array_like()) return;
        const Array& a = *n.array();
        if (n.type() == Type::Command && a.written_infix() && a.file().str() == file) ++c;
        for (const Node& k : a) self(self, k);
    };
    for (const Node& n : root) count(count, n);
    return c;
}

std::string prove_rewrite(std::string_view path, std::string_view original, std::string* summary) {
    auto read_as = [&](std::string_view text) {
        LoadSession session;
        session.set_pack_levels({"engine", "game", "task", "live"});
        return read_string(text, path, session);
    };
    auto before = read_as(original);
    if (!before) return "the original does not read: " + std::string(describe(before.error()));
    // No brace of the original was rewritten, so the switch changed nothing in its read.
    if (const int n = written_infix(**before, path); n != 0)
        return "the original reads " + std::to_string(n) + " braces as infix";
    auto text = read_text_file(path);
    if (!text) return std::string(text.error().message());
    auto after = read_as(*text);
    if (!after) return "the rewrite does not read: " + std::string(describe(after.error()));
    // The rewrite is the converter's: its tree, its marks.
    auto c = to_new(path, original);
    if (!c) return "the converter: " + std::string(describe(c.error()));
    if (std::string d = first_difference(*c->tree, **after); !d.empty())
        return "not the converter's tree: " + d;
    if (std::string d = first_mark_difference(*c->tree, **after); !d.empty()) return "marks: " + d;
    // The converter's tree is the original's but for the rewrites that rest on the
    // vocabulary (a band as a chain, a turned comparison, an assignment, a merged nest):
    // each undone, the tree is the original's exactly, and each evaluates as what it
    // replaced.
    std::vector<int> used(4, 0);
    std::vector<Node> kids;
    for (const Node& n : **after) kids.push_back(undo(n, c->rewrites, used));
    const Ref<Array> undone = Array::make_from(kids, (*after)->file(), (*after)->line());
    if (std::string d = first_difference(**before, *undone); !d.empty()) return "tree: " + d;
    int kinds[4] = {}, evaluated = 0, host = 0;
    for (const Rewrite& w : c->rewrites) {
        ++kinds[static_cast<size_t>(w.kind)];
        bool ran = false;
        if (std::string d = agree(w, ran); !d.empty()) return "meaning: " + d;
        ran ? ++evaluated : ++host;
    }
    for (size_t k = 0; k < 4; ++k)
        if (used[k] != kinds[k])
            return "rewrites: " + std::to_string(kinds[k]) + " of a kind recorded, " +
                   std::to_string(used[k]) + " found in the tree";
    // Comments and layout: every comment's text on its line, and the line count.
    const TextStructure x = read_structure(original);
    const TextStructure y = read_structure(*text);
    if (x.comments.size() != y.comments.size())
        return "comments: " + std::to_string(x.comments.size()) + " in the original, " +
               std::to_string(y.comments.size()) + " in the rewrite";
    for (size_t i = 0; i < x.comments.size(); ++i) {
        const TextComment& a = x.comments[i];
        const TextComment& b = y.comments[i];
        if (a.line != b.line || original.substr(a.begin, a.end - a.begin) !=
                                    std::string_view(*text).substr(b.begin, b.end - b.begin))
            return "comment " + std::to_string(i + 1) + " (line " + std::to_string(a.line) +
                   ") moved or changed";
    }
    const auto lines = [](std::string_view t) { return std::count(t.begin(), t.end(), '\n'); };
    if (lines(original) != lines(*text))
        return "lines: " + std::to_string(lines(original)) + " in the original, " +
               std::to_string(lines(*text)) + " in the rewrite";
    if (summary) {
        const size_t n = c->rewrites.size();
        *summary = "the converter's tree, " + std::to_string(written_infix(**after, path)) +
                   " commands written infix; the original's tree " +
                   (n == 0 ? std::string("exactly")
                           : "but for " + std::to_string(n) + " rewrites (bands " +
                                 std::to_string(kinds[0]) + ", turned " + std::to_string(kinds[1]) +
                                 ", assignments " + std::to_string(kinds[2]) + ", merged nests " +
                                 std::to_string(kinds[3]) + "), each undone exactly and " +
                                 std::to_string(evaluated) + " evaluated equal, " +
                                 std::to_string(host) + " calling the game's functions") +
                   "; " + std::to_string(x.comments.size()) + " comments on their lines, " +
                   std::to_string(lines(original)) + " lines";
    }
    return {};
}

std::string first_mark_difference(const Array& a, const Array& b) {
    std::string path, out;
    for (uint32_t i = 0; i < a.size() && i < b.size() && out.empty(); ++i) {
        path = std::to_string(i);
        mark_difference(a[i], b[i], path, out);
    }
    return out;
}

}  // namespace baker::experiments::syntax
