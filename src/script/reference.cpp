// The generated script reference and load-time checking: both read a Context's
// registrations, and neither evaluates anything.

#include "common/containers.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cstdio>

namespace moira {
namespace {

Str quoted(Symbol s) {
    return "'" + Str(s.str()) + "'";
}

bool evaluable(const Node& n) noexcept {
    return n.type() == Type::Command || n.type() == Type::Var || n.type() == Type::Property;
}

struct Checker {
    // A function defined by {func} or {override} in the checked tree.
    struct Defined {
        uint32_t arity;
        bool stub;
        Symbol file;
    };

    // What check() accumulates, as growable lists; it becomes the CheckReport at the end.
    struct DemandBuild {
        Symbol name;
        bool stub;
        Vec<CheckReport::Site> sites;
        Symbol on;
    };
    struct Build {
        Vec<Error> errors;
        Vec<DemandBuild> demand;
        Vec<Error> warnings;
    };

    Context::Impl& impl;
    Build& report;
    const LoadSession* packs;
    HashMap<Symbol, Defined> defined;
    HashMap<Symbol, Vec<Symbol>> classes;  // {class} in the tree: handlers
    HashMap<Symbol, Symbol> objects;       // {new Class name} in the tree
    HashMap<Str, size_t> demand_index;
    Vec<std::pair<Symbol, Symbol>> warned;  // (pack, function)

    static Str key(Symbol name, Symbol on) { return Str(on.str()) + "." + Str(name.str()); }

    void demand(Symbol name, bool stub, const Array& at, Symbol on = {}) {
        auto [it, inserted] = demand_index.try_emplace(key(name, on), report.demand.size());
        if (inserted) report.demand.push_back({name, stub, {}, on});
        report.demand[it->second].sites.push_back({at.file(), at.line()});
    }

    void error(const Array& at, Str message) {
        report.errors.push_back(
            make_error(ErrorCode::Type, std::move(message), at.file(), at.line()));
    }

    void collect(const Array& a, bool is_command) {
        if (a.height() > 1)
            for (const Node& n : a.nodes())
                if (const Array* c = n.array()) collect(*c, n.type() == Type::Command);
        if (!is_command || a.size() < 2) return;
        if (a[0].is("class") && a[1].try_sym()) {
            auto& handlers = classes[*a[1].try_sym()];
            for (size_t i = 2; i < a.size(); ++i)
                if (const Array* h = a[i].try_array(); h && !h->empty() && (*h)[0].try_sym())
                    handlers.push_back(*(*h)[0].try_sym());
            return;
        }
        if (a[0].is("new") && a.size() >= 3 && a[1].try_sym() && a[2].try_sym()) {
            objects[*a[2].try_sym()] = *a[1].try_sym();
            return;
        }
        if (a.size() < 3 || (!a[0].is("func") && !a[0].is("override"))) return;
        const auto name = a[1].try_sym();
        const Array* params = a[2].try_array();
        if (!name || !params) return;
        bool stub = false;
        for (size_t i = 3; i < a.size(); ++i) {
            const Array* m = a[i].try_array();
            if (!m || m->size() < 2) break;
            if ((*m)[0].is("status")) stub = (*m)[1].is("stub");
        }
        defined[*name] = {params->size(), stub, a.file()};
    }

    // Whether `o` accepts every literal argument of `command`, from index `first`.
    static bool fits(const detail::Overload& o, const Array& command, size_t first) {
        const size_t n = command.size() - first;
        for (size_t i = 0; i < n && i < o.params.size(); ++i) {
            const detail::ParamInfo& p = o.params[i];
            if (p.kind == detail::ParamKind::Rest) return true;
            if (!p.match || evaluable(command[i + first])) continue;
            if (!p.match(command[i + first], true)) return false;
        }
        return true;
    }

    // Arity and literal types against an overload set.
    void overloads(Symbol name, const Vec<detail::Overload>& set, const Array& command,
                   size_t first) {
        const uint32_t n = command.size() - static_cast<uint32_t>(first);
        const detail::Overload* arity_ok = nullptr;
        for (const detail::Overload& o : set) {
            if (n < o.min_args || n > o.max_args) continue;
            if (fits(o, command, first)) return;
            arity_ok = &o;
        }
        if (!arity_ok) {
            if (set.size() == 1)
                error(command, quoted(name) + " takes " +
                                   arity_text(set[0].min_args, set[0].max_args) + ", given " +
                                   detail::decimal(n));
            else
                error(command, "no overload of " + quoted(name) + " takes " + arity_text(n, n));
            return;
        }
        Str given;
        for (size_t i = first; i < command.size(); ++i) {
            if (i > first) given += ' ';
            given += evaluable(command[i]) ? "?" : type_name(command[i].type());
        }
        Str have;
        for (const detail::Overload& o : set)
            if (n >= o.min_args && n <= o.max_args) have += " " + signature_text(o);
        error(command, quoted(name) + " is given (" + given + "); it takes" + have);
    }

    // {object method ...} where the object's class is known: a named object, or one the
    // tree creates with {new Class name}.
    void send(const Array& command, Symbol cls, const detail::TypeRecord* type) {
        const auto method = command.size() > 1 ? command[1].try_sym() : std::nullopt;
        if (!method) return;
        for (const char* m : {"get", "set", "type", "name"})
            if (*method == Symbol(m)) return;
        auto handles = [&](Symbol c) {
            if (auto it = classes.find(c); it != classes.end())
                for (Symbol h : it->second)
                    if (h == *method) return true;
            const ScriptClass* sc = impl.script_class(c);
            return sc && sc->handler(*method);
        };
        if (handles(cls)) return;
        if (type) {
            for (const detail::TypeRecord* b : type->bases)
                if (handles(b->symbol)) return;
            if (const detail::MethodEntry* m = type->method(*method)) {
                if (m->status == Status::Stub) demand(*method, true, command, cls);
                overloads(*method, m->overloads, command, 2);
                return;
            }
        }
        demand(*method, false, command, cls);
    }

    // The one-way rule (D80): softer may call harder, never the reverse.
    void one_way(const Array& command, Symbol name, Symbol defined_in) {
        const Pack* mine = packs->pack_of(command.file());
        const Pack* theirs = mine ? packs->pack_of(defined_in) : nullptr;
        if (!theirs || theirs->rank <= mine->rank) return;
        const std::pair<Symbol, Symbol> key{mine->name, name};
        if (std::find(warned.begin(), warned.end(), key) != warned.end()) return;
        warned.push_back(key);
        report.warnings.push_back(
            make_error(ErrorCode::Refused,
                       "pack " + quoted(mine->name) + " (level " + Str(mine->level.str()) +
                           ") calls " + quoted(name) + " from the softer pack " +
                           quoted(theirs->name) + " (level " + Str(theirs->level.str()) + ")",
                       command.file(), command.line()));
    }

    void call(const Array& command) {
        const auto name = command[0].try_sym();
        if (!name) return;  // an object computed at run time
        const uint32_t n = command.size() - 1;
        const Function* f = impl.find(*name);
        if (packs) {
            if (auto it = defined.find(*name); it != defined.end())
                one_way(command, *name, it->second.file);
            else if (f && f->tier == Tier::Script)
                one_way(command, *name, f->file);
        }
        if (f && !f->overloads.empty()) {
            if (f->status == Status::Stub) demand(*name, true, command);
            overloads(*name, f->overloads, command, 1);
            return;
        }
        if (const Context::Impl::Slot* s = impl.slot(impl.named(*name))) {
            send(command, Symbol(impl.class_name(*s)), s->script ? nullptr : &s->object->type());
            return;
        }
        if (auto it = objects.find(*name); it != objects.end()) {
            send(command, it->second, impl.type_named(it->second));
            return;
        }
        if (auto it = defined.find(*name); it != defined.end()) {
            if (it->second.stub) demand(*name, true, command);
            if (n != it->second.arity)
                error(command, quoted(*name) + " takes " +
                                   arity_text(it->second.arity, it->second.arity) + ", given " +
                                   detail::decimal(n));
            return;
        }
        demand(*name, false, command);
    }

    void walk(const Array& a, bool is_command) {
        if (is_command && !a.empty()) call(a);
        if (a.height() > 1)
            for (const Node& n : a.nodes())
                if (const Array* c = n.array()) walk(*c, n.type() == Type::Command);
    }
};

}  // namespace

void check(const Array& tree, const Context& ctx, CheckReport& report, const LoadSession* packs) {
    // The report accumulates: start from what it holds.
    Checker::Build b;
    b.errors.assign(report.errors.begin(), report.errors.end());
    b.warnings.assign(report.warnings.begin(), report.warnings.end());
    for (const CheckReport::Demand& d : report.demand)
        b.demand.push_back({d.name, d.stub, {d.sites.begin(), d.sites.end()}, d.on});
    Checker c{ctx.impl(), b, packs, {}, {}, {}, {}, {}};
    for (size_t i = 0; i < b.demand.size(); ++i)
        c.demand_index[Checker::key(b.demand[i].name, b.demand[i].on)] = i;
    c.collect(tree, false);
    c.walk(tree, false);
    Vec<CheckReport::Demand> demand;
    for (const Checker::DemandBuild& d : b.demand)
        demand.push_back({d.name, d.stub, List<CheckReport::Site>(d.sites), d.on});
    report.errors = List<Error>(b.errors);
    report.warnings = List<Error>(b.warnings);
    report.demand = List<CheckReport::Demand>(demand);
}

CheckReport check(const Array& tree, const Context& ctx, const LoadSession* packs) {
    CheckReport report;
    check(tree, ctx, report, packs);
    return report;
}

void write(const CheckReport& report, TextSink text) {
    Str out;
    for (const Error& e : report.errors) out += Str(moira::to_string(e)) + "\n";
    for (const Error& w : report.warnings) out += "warning: " + Str(moira::to_string(w)) + "\n";
    for (const CheckReport::Demand& d : report.demand) {
        out += "demand: " + quoted(d.name);
        if (!d.on.empty()) out += " on " + Str(d.on.str());
        out += Str(d.stub ? " is a stub" : " is not defined") + ", called at";
        for (size_t i = 0; i < d.sites.size(); ++i) {
            out += i ? ", " : " ";
            out += Str(d.sites[i].file.str()) + ":" + detail::decimal(d.sites[i].line);
        }
        out += "\n";
    }
    text(out);
}

namespace {
Str reference_text(const Context& ctx) {
    const Context::Impl& impl = ctx.impl();
    Vec<const Function*> fs;
    for (const auto& f : impl.functions)
        if (!f->overloads.empty()) fs.push_back(f.get());
    std::sort(fs.begin(), fs.end(),
              [](const Function* a, const Function* b) { return a->name.str() < b->name.str(); });
    Str out =
        "# Script reference\n\n"
        "Generated by `moira::reference()` from what a Context registers. Do not edit it by\n"
        "hand: the test suite regenerates it and fails when it and the code disagree.\n\n"
        "A parameter in [brackets] is optional, `unevaluated` is passed as written, and\n"
        "`values...` takes the rest of the arguments.\n\n## Functions\n";
    for (const Function* f : fs) {
        out += "\n### `" + Str(f->name.str()) + "`\n\n";
        for (const detail::Overload& o : f->overloads) {
            Str sig = signature_text(o);
            sig = sig.size() > 2 ? " " + sig.substr(1, sig.size() - 2) : "";
            out += "- `{" + Str(f->name.str()) + sig + "}` → " + Str(o.result) + "\n";
        }
        out += "\n";
        out += f->tier == Tier::Native ? "*Native" : "*Script";
        switch (f->status) {
            case Status::Normal: break;
            case Status::Stub: out += ", stub"; break;
            case Status::Provisional: out += ", provisional"; break;
            case Status::Deprecated:
                out += ", deprecated";
                if (!f->replacement.empty()) out += ": use `" + Str(f->replacement.str()) + "`";
                break;
        }
        out += ".*";
        if (!f->doc.empty()) out += " " + f->doc;
        out += "\n";
    }
    Vec<const detail::TypeRecord*> types = impl.types;
    Vec<const ScriptClass*> scripts;
    for (const auto& c : impl.classes)
        if (!impl.type_named(c->name)) scripts.push_back(c.get());
    if (!types.empty() || !scripts.empty()) {
        std::sort(types.begin(), types.end(),
                  [](const auto* a, const auto* b) { return a->name < b->name; });
        std::sort(scripts.begin(), scripts.end(),
                  [](const auto* a, const auto* b) { return a->name.str() < b->name.str(); });
        out +=
            "\n## Types\n\nEvery object also has `{obj get name}`, `{obj set name value}`, "
            "`{obj type}` and `{obj name}`.\n";
        auto handlers = [&](Symbol cls, const Str& shown) {
            if (const ScriptClass* c = impl.script_class(cls))
                for (const ScriptClass::Handler& h : c->handlers) {
                    out += "- `{<" + shown + "> " + Str(h.name.str());
                    for (Symbol p : h.body.params) out += " $" + Str(p.str());
                    out += "}` (script)\n";
                }
        };
        for (const detail::TypeRecord* t : types) {
            out += "\n### `" + t->name + "`\n\n";
            if (!t->bases.empty()) out += "Derives from `" + t->bases[0]->name + "`.\n\n";
            Vec<const detail::MethodEntry*> ms;
            for (const detail::MethodEntry& m : t->methods) ms.push_back(&m);
            std::sort(ms.begin(), ms.end(),
                      [](const auto* a, const auto* b) { return a->name.str() < b->name.str(); });
            for (const detail::MethodEntry* m : ms)
                for (const detail::Overload& o : m->overloads) {
                    Str sig = signature_text(o);
                    sig = sig.size() > 2 ? " " + sig.substr(1, sig.size() - 2) : "";
                    out += "- `{<" + t->name + "> " + Str(m->name.str()) + sig + "}` → " +
                           Str(o.result);
                    if (&o == &m->overloads.front() && !m->doc.empty()) out += ": " + m->doc;
                    if (!m->message.empty()) out += " (message `" + Str(m->message) + "`)";
                    out += "\n";
                }
            for (const detail::PropertyEntry& p : t->properties)
                out += "- property `" + Str(p.name.str()) + "`: " + Str(p.type) +
                       (p.doc.empty() ? "" : ": " + p.doc) + "\n";
            handlers(t->symbol, t->name);
        }
        for (const ScriptClass* c : scripts) {
            out += "\n### `" + Str(c->name.str()) + "` (script class)\n\n";
            handlers(c->name, Str(c->name.str()));
        }
    }
    if (!impl.globals.empty()) {
        Vec<Global> gs = impl.globals;
        std::sort(gs.begin(), gs.end(),
                  [](const Global& a, const Global& b) { return a.name.str() < b.name.str(); });
        out += "\n## Globals\n";
        for (const Global& g : gs) {
            out += "\n### `$" + Str(g.name.str()) + "`\n";
            if (!g.doc.empty()) out += "\n" + g.doc + "\n";
        }
    }
    return out;
}
}  // namespace

void reference(const Context& ctx, TextSink out) {
    out(reference_text(ctx));
}

Expected<void> write_reference(const Context& ctx, std::string_view path) {
    const Str text = reference_text(ctx);
    const Str p(path);
    std::FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return Unexpected(make_error(ErrorCode::Io, "cannot write " + p));
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok)
        return Unexpected(make_error(ErrorCode::Io, "cannot write " + p));
    return {};
}

}  // namespace moira
