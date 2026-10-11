// Context: dispatch, variables, overload resolution, and registration.
#include "internal.hpp"

#include <moira/parse.hpp>

#include "common/containers.hpp"

#include <cstdio>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace moira {
namespace {

thread_local Context* t_current = nullptr;

// A cell that has never been set holds this: a Func node with no record, which nothing
// else can produce.
const Node kUnset = Node::func(nullptr);

bool is_unset(const Node& n) noexcept {
    return n.type() == Type::Func && !n.func_record();
}

Str quoted(Symbol s) {
    return "'" + Str(s.str()) + "'";
}

// Errors are built out of line, so the evaluator's frames, which nest once per script call,
// hold no message temporaries.
#if defined(_MSC_VER)
#define MOIRA_COLD __declspec(noinline)
#else
#define MOIRA_COLD [[gnu::noinline, gnu::cold]]
#endif

}  // namespace

// The stack is recorded where an error is made or first passes a call, with every frame
// still live; an error that has one keeps it as it unwinds further. Nothing on the
// successful path pays for it.
MOIRA_COLD void record_stack(Error& e) {
    if (!e.stack().empty() || !t_current) return;
    for (const CallFrame* c = t_current->impl().top; c; c = c->parent)
        e.push_stack(t_current->stack_entry(*c));
}

namespace {

MOIRA_COLD void locate(Error& e, const Array& at) {
    if (e.file().empty()) {
        e.set_location(at.file(), at.line());
    }
    record_stack(e);
}

MOIRA_COLD Expected<Node> fail(const Array& at, ErrorCode code, Str message) {
    Error e = make_error(code, std::move(message), at.file(), at.line());
    record_stack(e);
    return Unexpected(std::move(e));
}

MOIRA_COLD Expected<Node> bad_head(const Array& command) {
    if (command.empty()) return fail(command, ErrorCode::Syntax, "empty command {}");
    return fail(command, ErrorCode::Type,
                "a command starts with a function or an object, not a " +
                    Str(type_name(command[0].type())));
}

MOIRA_COLD Expected<Node> not_function(const Array& command, Symbol name) {
    return fail(command, ErrorCode::NotFound, quoted(name) + " is not a function");
}

MOIRA_COLD Expected<Node> too_deep(const Array& command, uint32_t depth, uintptr_t used) {
    return fail(command, ErrorCode::Limit,
                "calls nested too deep (" + detail::decimal(depth) + " calls, " +
                    detail::decimal(used / 1024) + " KB of stack)");
}

MOIRA_COLD Expected<Node> bad_arity(const Array& command, Symbol name, const detail::Overload& o,
                                    size_t n) {
    return fail(command, ErrorCode::Type,
                quoted(name) + " takes " + arity_text(o.min_args, o.max_args) + ", given " +
                    detail::decimal(n));
}

MOIRA_COLD Expected<Node> no_overload(const Array& command, Symbol name,
                                      const Vec<detail::Overload>& overloads, const Node* values,
                                      size_t n) {
    Str given;
    for (size_t i = 0; i < n; ++i) {
        if (i) given += ' ';
        given += type_name(values[i].type());
    }
    Str have;
    for (const detail::Overload& o : overloads) have += " " + signature_text(o);
    return fail(command, ErrorCode::Type,
                "no overload of " + quoted(name) + " takes (" + given + "); it has" + have);
}

MOIRA_COLD Expected<Node> not_object(const Array& command, const Node& head) {
    return fail(
        command, ErrorCode::Type,
        "a command starts with a function or an object, not a " + Str(type_name(head.type())));
}

MOIRA_COLD Expected<Node> not_set(Symbol name) {
    return Unexpected(make_error(ErrorCode::NotFound, "$" + Str(name.str()) + " is not set"));
}

// Calls `o`, locating an error at the call. One return, so the result is never moved.
Expected<Node> invoke(const detail::Overload& o, Args& args, const Array& command) {
    Expected<Node> r = o.thunk(o.callable.get(), args);
    if (!r) locate(r.error(), command);
    return r;
}

// Evaluates a Command or Var for moira::data's typed lookups.
Expected<Node> evaluate_for_data(const Node& n) {
    if (!t_current)
        return Unexpected(make_error(ErrorCode::NotFound,
                                     "a " + Str(type_name(n.type())) +
                                         " value needs a current moira::Context on this thread"));
    return t_current->eval(n);
}

void stderr_sink(void*, const Error& notice) {
    const std::string_view text = describe(notice);
    std::fprintf(stderr, "%.*s\n", static_cast<int>(text.size()), text.data());
}

}  // namespace

// --- Signatures ---

Str signature_text(const detail::Overload& o) {
    Str out = "(";
    for (const detail::ParamInfo& p : o.params) {
        if (out.size() > 1) out += ' ';
        if (p.kind == detail::ParamKind::Optional) {
            out += '[';
            out += p.type;
            out += ']';
        } else {
            out += p.type;
        }
    }
    return out + ")";
}

Str arity_text(uint32_t min, uint32_t max) {
    Str n;
    if (max == UINT32_MAX)
        n = "at least " + detail::decimal(min);
    else if (min == max)
        n = detail::decimal(min);
    else
        n = detail::decimal(min) + " to " + detail::decimal(max);
    return n + (n == "1" || n == "at least 1" ? " argument" : " arguments");
}

// --- Impl ---

Node* Context::Impl::cell(Symbol name) {
    const uint32_t i = name.index();
    const uint32_t page = i >> kPageBits;
    if (page >= pages.size()) pages.resize(page + 1);
    if (!pages[page]) {
        constexpr size_t cells = size_t{1} << kPageBits;
        auto* p = static_cast<Node*>(detail::allocate(alloc, cells * sizeof(Node), alignof(Node)));
        for (size_t k = 0; k < cells; ++k) ::new (p + k) Node(kUnset);
        pages[page] = p;
    }
    return &pages[page][i & ((1u << kPageBits) - 1)];
}

Context::Impl::~Impl() {
    constexpr size_t cells = size_t{1} << kPageBits;
    for (Node* p : pages) {
        if (!p) continue;
        for (size_t k = 0; k < cells; ++k) p[k].~Node();
        detail::deallocate(alloc, p, cells * sizeof(Node));
    }
}

Function& Context::Impl::function(Symbol name) {
    if (Function* f = find(name)) return *f;
    functions.push_back(Own<Function>::make(alloc, alloc));
    Function& f = *functions.back();
    f.name = name;
    if (name.index() >= by_index.size()) by_index.resize(name.index() + 1, nullptr);
    by_index[name.index()] = &f;
    return f;
}

Expected<Node> Context::Impl::eval(const Node& n) {
    switch (n.type()) {
        case Type::Command: return call(*n.array());
        case Type::Var: {
            const Node* c = cell_if(n.var_name());
            if (c && !is_unset(*c)) return *c;
            return not_set(n.var_name());
        }
        case Type::Property: return property_path(*n.array());
        default: return n;
    }
}

Expected<Node> Context::Impl::call(const Array& command) {
    if (command.empty()) return bad_head(command);
    // The head names a function, or is an object: named, an Object node, or a command or
    // variable that evaluates to one (spec §5, "Object commands evaluate slot 0").
    const Node& head = command[0];
    const Function* f = nullptr;
    ObjectHandle target;
    Symbol name;
    if (head.type() == Type::Symbol) {
        name = *head.try_sym();
        f = find(name);
        if (!f || f->overloads.empty()) {
            f = nullptr;
            target = named(name);
            if (!target.bits) return not_function(command, name);
        }
    } else if (head.type() == Type::Object) {
        target = head.object_handle();
    } else if (head.type() == Type::Command || head.type() == Type::Var) {
        Expected<Node> r = eval(head);
        if (!r) {
            locate(r.error(), command);
            return r;
        }
        if (r->type() != Type::Object) return not_object(command, *r);
        target = r->object_handle();
    } else {
        return bad_head(command);
    }
    // Deep recursion is an error, not a crash: at most kMaxDepth calls, and at most
    // stack_budget bytes of stack below the outermost one (a debug build's frames are large).
    // The frame's address, not a local's: ASan can move address-taken locals to the heap.
#if defined(_MSC_VER)
    const auto at = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
#else
    const auto at = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
#endif
    if (depth == 0) stack_base = at;
    const uintptr_t used = at < stack_base ? stack_base - at : at - stack_base;
    if (depth >= kMaxDepth || used > stack_budget) return too_deep(command, depth, used);
    // This call joins the frame list for as long as it runs (spec §5, "Debugging").
    struct Frame {
        Impl& impl;
        size_t kept;
        CallFrame call;
        ~Frame() {
            --impl.depth;
            impl.top = call.parent;
            if (impl.kept.size() > kept) impl.kept.resize(kept);
        }
    } frame{*this, kept.size(), {&command, top}};
    top = &frame.call;
    ++depth;
    if (!f) return send(target, command);
    if (f->overloads.size() > 1) return invoke_set(name, f->overloads, command, 1, nullptr);
    // One overload: the common case, kept inline.
    const detail::Overload& o = f->overloads[0];
    const size_t n = command.size() - 1;
    if (n < o.min_args || n > o.max_args) return bad_arity(command, name, o, n);
    Args args(self, command, command.begin() + 1, n, 0, false);
    return invoke(o, args, command);
}

// One overload is called directly, its arguments evaluated only as it reads them. An
// overload set evaluates every argument first, then picks the first overload whose
// parameter types fit: exactly, then with Int read as Float.
Expected<Node> Context::Impl::invoke_set(Symbol name, const Vec<detail::Overload>& overloads,
                                         const Array& command, size_t first, ObjectBase* object) {
    const size_t n = command.size() - first;
    if (overloads.size() == 1) {
        const detail::Overload& o = overloads[0];
        if (n < o.min_args || n > o.max_args) return bad_arity(command, name, o, n);
        Args args(self, command, command.begin() + first, n, 0, false, first, object);
        return invoke(o, args, command);
    }
    // Eight on the stack, more from the default allocator. Not a Vec: an empty std::vector
    // allocates in MSVC's debug library. Not a NodeBuffer: it costs 70 instructions a call.
    struct Spill {
        Node* p = nullptr;
        size_t n = 0;
        ~Spill() {
            for (size_t i = 0; i < n; ++i) p[i].~Node();
            if (p) detail::default_free(p, n * sizeof(Node));
        }
    } spill;
    Node small[8];
    Node* values = small;
    if (n > 8) {
        spill.p = static_cast<Node*>(detail::default_alloc(n * sizeof(Node), alignof(Node)));
        for (; spill.n < n; ++spill.n) ::new (spill.p + spill.n) Node();
        values = spill.p;
    }
    for (size_t i = 0; i < n; ++i) {
        auto r = eval(command[i + first]);
        if (!r) {
            locate(r.error(), command);
            return r;
        }
        values[i] = std::move(*r);
    }
    auto fits = [&](const detail::Overload& o, bool promote) {
        if (n < o.min_args || n > o.max_args) return false;
        for (size_t i = 0; i < n && i < o.params.size(); ++i) {
            const detail::ParamInfo& p = o.params[i];
            if (p.kind == detail::ParamKind::Rest) return true;
            if (p.match && !p.match(values[i], promote)) return false;
        }
        return true;
    };
    for (const bool promote : {false, true}) {
        for (const detail::Overload& o : overloads) {
            if (!fits(o, promote)) continue;
            Args args(self, command, values, n, 0, true, first, object);
            return invoke(o, args, command);
        }
    }
    return no_overload(command, name, overloads, values, n);
}

Expected<Node> Context::Impl::run(std::span<const Node> nodes) {
    Node last;
    for (const Node& n : nodes) {
        auto r = eval(n);
        if (!r) return r;
        last = std::move(*r);
    }
    return last;
}

// --- Context ---

Context::Context(const Allocator& allocator)
    : impl_(detail::create<Impl>(allocator, *this, allocator)) {
    detail::set_evaluator(&evaluate_for_data);
    if (!t_current) t_current = this;
    impl_->sink = &stderr_sink;
    register_builtins(*this);
    register_object_builtins(*this);
}

Context::~Context() {
    // Objects the Context created are deleted; the others just forget it.
    for (Impl::Slot& s : impl_->slots) {
        if (!s.object) continue;
        s.object->ctx_ = nullptr;
        if (s.owned) impl_->destroy_owned(s.object);
    }
    if (t_current == this) t_current = nullptr;
    const Allocator alloc = impl_->alloc;
    detail::destroy(alloc, impl_);
}

Context::Scope::Scope(Context& ctx) noexcept : previous_(t_current) {
    t_current = &ctx;
}

Context::Scope::~Scope() {
    t_current = previous_;
}

Context* Context::current() noexcept {
    return t_current;
}

Expected<Node> Context::eval(const Node& node) {
    Scope scope(*this);
    return impl_->eval(node);
}

Expected<Node> Context::run(std::span<const Node> nodes) {
    Scope scope(*this);
    return impl_->run(nodes);
}

Expected<Node> Context::eval(std::string_view text, std::string_view name) {
    auto tree = read_string(text, name);
    if (!tree) return Unexpected(std::move(tree).error());
    return run((*tree)->nodes());
}

bool Context::has_func(Symbol name) const noexcept {
    const Function* f = impl_->find(name);
    return f && !f->overloads.empty();
}

VarRef Context::var(Symbol name) {
    return VarRef(impl_->cell(name), name);
}

VarRef Context::global(Symbol name, Node initial, std::string_view doc) {
    VarRef v = var(name);
    v = std::move(initial);
    for (Global& g : impl_->globals)
        if (g.name == name) {
            g.doc = doc;
            return v;
        }
    impl_->globals.push_back({name, Str(doc)});
    return v;
}

void Context::set_stack_budget(size_t bytes) noexcept {
    impl_->stack_budget = bytes;
}

// --- Debugging ---

const CallFrame* Context::top_frame() const noexcept {
    return impl_->top;
}

StackEntry Context::stack_entry(const CallFrame& frame) const {
    const Array& c = *frame.command;
    StackEntry e{{}, {}, c.file(), c.line()};
    if (c.empty()) return e;
    const auto head = c[0].try_sym();
    if (head && has_func(*head)) {
        e.function = *head;
        return e;
    }
    // An object command: its method, and the object's name if it has one.
    if (c.size() > 1) e.function = c[1].try_sym().value_or(Symbol());
    if (head) {
        e.on = *head;
    } else if (c[0].type() == Type::Object) {
        if (const Impl::Slot* s = impl_->slot(c[0].object_handle())) e.on = s->name;
    }
    return e;
}

List<StackEntry> Context::backtrace() const {
    Vec<StackEntry> out;
    for (const CallFrame* c = impl_->top; c; c = c->parent) out.push_back(stack_entry(*c));
    return List<StackEntry>(out);
}

void script_backtrace(TextOut out) {
    if (const Context* ctx = Context::current())
        for (const CallFrame* c = ctx->top_frame(); c; c = c->parent) {
            out("in ");
            write(ctx->stack_entry(*c), out);
            out("\n");
        }
}

void Context::set_notice_sink(NoticeSink sink, void* user) noexcept {
    impl_->sink = sink;
    impl_->sink_user = user;
}

void Context::notice(Error notice) const {
    if (impl_->sink) impl_->sink(impl_->sink_user, notice);
}

Expected<FuncRef> Context::add_overload(Symbol name, detail::Overload o) {
    auto has_raw = [](const detail::Overload& x) {
        for (const detail::ParamInfo& p : x.params)
            if (p.raw) return true;
        return false;
    };
    Function& f = impl_->function(name);
    if (f.tier == Tier::Script && !f.overloads.empty()) {
        const uint32_t n = f.overloads[0].min_args;
        Error e = make_error(ErrorCode::Refused,
                             "native " + quoted(name) + " replaces the script definition", f.file,
                             f.line);
        if (n < o.min_args || n > o.max_args)
            e.set_message(Str(e.message()) + "; the script took " + arity_text(n, n) +
                          ", the native takes " + arity_text(o.min_args, o.max_args));
        notice(std::move(e));
        f.overloads.clear();
        f.status = Status::Normal;
        f.doc.clear();
    }
    for (const detail::Overload& x : f.overloads) {
        if (has_raw(x) || has_raw(o))
            return Unexpected(make_error(
                ErrorCode::Refused, quoted(name) + " takes unevaluated arguments, so it cannot be "
                                                   "overloaded"));
        if (x.min_args == o.min_args && x.max_args == o.max_args &&
            signature_text(x) == signature_text(o))
            return Unexpected(
                make_error(ErrorCode::Refused,
                           quoted(name) + " already has the overload " + signature_text(o)));
    }
    f.tier = Tier::Native;
    f.overloads.push_back(std::move(o));
    return FuncRef(&f);
}

FuncRef& FuncRef::doc(std::string_view text) {
    static_cast<Function*>(f_)->doc = text;
    return *this;
}

FuncRef& FuncRef::status(Status status, Symbol replacement) {
    auto* f = static_cast<Function*>(f_);
    f->status = status;
    f->replacement = replacement;
    return *this;
}

// --- Args, Raw, VarRef ---

Expected<Node> Args::eval_node(const Node& n) const {
    Expected<Node> r = ctx_->impl().eval(n);
    if (!r) locate(r.error(), *command_);
    return r;
}

Expected<Node> Args::eval(size_t i) const {
    if (i >= size_) return Unexpected(type_error(i, Node(), "value"));
    if (evaluated_) return base_[i];
    return eval_node(base_[i]);
}

Expected<const Array*> Args::array(size_t i) const {
    auto r = eval(i);
    if (!r) return Unexpected(std::move(r).error());
    if (r->type() != Type::Array) return Unexpected(type_error(i, *r, "array"));
    const Array* a = r->try_array();
    keep(std::move(*r));
    return a;
}

Args Args::rest(size_t i) const noexcept {
    const size_t k = i < size_ ? i : size_;
    return Args(*ctx_, *command_, base_ + k, size_ - k, offset_ + k, evaluated_, first_, self_);
}

Symbol Args::name() const noexcept {
    const Node* n = command_->try_at(first_ - 1u);
    return n ? n->try_sym().value_or(Symbol()) : Symbol();
}

Error Args::error(std::string_view message, ErrorCode code) const {
    return make_error(code, message, command_->file(), command_->line());
}

Error Args::type_error(size_t i, const Node& n, std::string_view want) const {
    const Str which = "argument " + detail::decimal(offset_ + i + 1) + " of " + quoted(name());
    if (i >= size_)
        return error(quoted(name()) + " is missing " + which.substr(0, which.find(" of")));
    return error(which + " is " + Str(type_name(n.type())) + ", not " + Str(want));
}

void Args::keep(Node n) const {
    ctx_->impl().kept.push_back(std::move(n));
}

namespace detail {
Expected<Raw<Var>> read_var(const Args& a, size_t i) {
    const Node& n = a.raw(i);
    if (n.type() == Type::Property) return Raw<Var>(a.context(), *n.array());
    if (n.type() != Type::Var)
        return Unexpected(a.error("argument " + detail::decimal(i + 1) + " of " + quoted(a.name()) +
                                  " must be a $variable, not " + Str(type_name(n.type()))));
    return Raw<Var>(a.context().impl().cell(n.var_name()), n.var_name());
}
}  // namespace detail

Expected<Node> Raw<Node>::eval() const {
    return ctx_->impl().eval(*node_);
}

bool VarRef::is_set() const noexcept {
    return !is_unset(*cell_);
}

const Node& VarRef::value() const noexcept {
    static const Node kZero;
    return is_unset(*cell_) ? kZero : *cell_;
}

void VarRef::unset() noexcept {
    *cell_ = kUnset;
}

}  // namespace moira
