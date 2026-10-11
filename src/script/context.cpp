// Context: dispatch, variables, overload resolution, and registration.
#include "internal.hpp"

#include <moira/parse.hpp>

#include <cstdio>

namespace moira {
namespace {

thread_local Context* t_current = nullptr;

// A cell that has never been set holds this: a Func node with no record, which nothing
// else can produce.
const Node kUnset = Node::func(nullptr);

bool is_unset(const Node& n) noexcept {
    return n.type() == Type::Func && !n.func_record();
}

Error located(Error e, const Array& at) {
    if (e.file.empty()) {
        e.file = at.file();
        e.line = at.line();
    }
    return e;
}

// Calls `o`, locating an error at the call. One return, so the result is never moved.
Expected<Node> invoke(const detail::Overload& o, Args& args, const Array& command) {
    Expected<Node> r = o.thunk(o.callable.get(), args);
    if (!r) r = Unexpected(located(std::move(r).error(), command));
    return r;
}

std::string quoted(Symbol s) {
    return "'" + std::string(s.str()) + "'";
}

// Evaluates a Command or Var for moira::data's typed lookups.
Expected<Node> evaluate_for_data(const Node& n) {
    if (!t_current)
        return Unexpected(make_error(ErrorCode::NotFound,
                                     "a " + std::string(type_name(n.type())) +
                                         " value needs a current moira::Context on this thread"));
    return t_current->try_eval(n);
}

void stderr_sink(void*, const Error& notice) {
    std::fprintf(stderr, "%s\n", to_string(notice).c_str());
}

}  // namespace

// --- Signatures ---

std::string signature_text(const detail::Overload& o) {
    std::string out = "(";
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

std::string arity_text(uint32_t min, uint32_t max) {
    std::string n;
    if (max == UINT32_MAX)
        n = "at least " + std::to_string(min);
    else if (min == max)
        n = std::to_string(min);
    else
        n = std::to_string(min) + " to " + std::to_string(max);
    return n + (n == "1" || n == "at least 1" ? " argument" : " arguments");
}

// --- Impl ---

Node* Context::Impl::cell(Symbol name) {
    const uint32_t i = name.index();
    const uint32_t page = i >> kPageBits;
    if (page >= pages.size()) pages.resize(page + 1);
    if (!pages[page]) {
        pages[page] = std::make_unique<Node[]>(size_t{1} << kPageBits);
        for (size_t k = 0; k < (size_t{1} << kPageBits); ++k) pages[page][k] = kUnset;
    }
    return &pages[page][i & ((1u << kPageBits) - 1)];
}

Function& Context::Impl::function(Symbol name) {
    if (Function* f = find(name)) return *f;
    functions.push_back(std::make_unique<Function>());
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
            return Unexpected(make_error(ErrorCode::NotFound,
                                         "$" + std::string(n.var_name().str()) + " is not set"));
        }
        case Type::Property:
            return Unexpected(make_error(ErrorCode::Type,
                                         "[...] property access is not supported yet",
                                         n.array()->file(), n.array()->line()));
        default: return n;
    }
}

Expected<Node> Context::Impl::call(const Array& command) {
    if (command.empty())
        return Unexpected(located(make_error(ErrorCode::Syntax, "empty command {}"), command));
    const Node& head = command[0];
    if (head.type() != Type::Symbol)
        return Unexpected(
            located(make_error(ErrorCode::Type, "a command starts with a function name, not a " +
                                                    std::string(type_name(head.type()))),
                    command));
    const Symbol name = *head.try_sym();
    const Function* f = find(name);
    if (!f || f->overloads.empty())
        return Unexpected(
            located(make_error(ErrorCode::NotFound, quoted(name) + " is not a function"), command));
    if (depth >= kMaxDepth)
        return Unexpected(
            located(make_error(ErrorCode::Limit,
                               "calls nested more than " + std::to_string(kMaxDepth) + " deep"),
                    command));
    struct Frame {
        Impl& impl;
        size_t kept;
        ~Frame() {
            --impl.depth;
            if (impl.kept.size() > kept) impl.kept.resize(kept);
        }
    } frame{*this, kept.size()};
    ++depth;
    if (f->overloads.size() > 1) return call_overloaded(*f, command);
    const detail::Overload& o = f->overloads[0];
    const size_t n = command.size() - 1;
    if (n < o.min_args || n > o.max_args)
        return Unexpected(
            located(make_error(ErrorCode::Type, quoted(name) + " takes " +
                                                    arity_text(o.min_args, o.max_args) +
                                                    ", given " + std::to_string(n)),
                    command));
    Args args(self, command, command.begin() + 1, n, 0, false);
    return invoke(o, args, command);
}

// An overload set evaluates every argument first, then picks the first overload whose
// parameter types fit: exactly, then with Int read as Float.
Expected<Node> Context::Impl::call_overloaded(const Function& f, const Array& command) {
    const size_t n = command.size() - 1;
    Node small[8];
    std::unique_ptr<Node[]> large;
    Node* values = n <= 8 ? small : (large = std::make_unique<Node[]>(n)).get();
    for (size_t i = 0; i < n; ++i) {
        auto r = eval(command[i + 1]);
        if (!r) return Unexpected(located(std::move(r).error(), command));
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
        for (const detail::Overload& o : f.overloads) {
            if (!fits(o, promote)) continue;
            Args args(self, command, values, n, 0, true);
            return invoke(o, args, command);
        }
    }
    std::string given;
    for (size_t i = 0; i < n; ++i) {
        if (i) given += ' ';
        given += type_name(values[i].type());
    }
    std::string have;
    for (const detail::Overload& o : f.overloads) have += " " + signature_text(o);
    return Unexpected(located(
        make_error(ErrorCode::Type,
                   "no overload of " + quoted(f.name) + " takes (" + given + "); it has" + have),
        command));
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

Context::Context() : impl_(std::make_unique<Impl>(*this)) {
    detail::set_evaluator(&evaluate_for_data);
    if (!t_current) t_current = this;
    impl_->sink = &stderr_sink;
    register_builtins(*this);
}

Context::~Context() {
    if (t_current == this) t_current = nullptr;
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

Expected<Node> Context::try_eval(const Node& node) {
    Scope scope(*this);
#if MOIRA_EXCEPTIONS
    try {
        return impl_->eval(node);
    } catch (const Exception& e) {
        return Unexpected(e.error());
    }
#else
    return impl_->eval(node);
#endif
}

Expected<Node> Context::try_run(std::span<const Node> nodes) {
    Scope scope(*this);
#if MOIRA_EXCEPTIONS
    try {
        return impl_->run(nodes);
    } catch (const Exception& e) {
        return Unexpected(e.error());
    }
#else
    return impl_->run(nodes);
#endif
}

Expected<Node> Context::try_eval(std::string_view text, std::string_view name) {
    auto tree = try_read_string(text, name);
    if (!tree) return Unexpected(std::move(tree).error());
    return try_run((*tree)->nodes());
}

#if MOIRA_EXCEPTIONS
namespace {
Node value_or_raise(Expected<Node>&& r) {
    if (!r) detail::raise(std::move(r).error());
    return std::move(*r);
}
}  // namespace

Node Context::eval(const Node& node) {
    return value_or_raise(try_eval(node));
}
Node Context::eval(std::string_view text, std::string_view name) {
    return value_or_raise(try_eval(text, name));
}
Node Context::run(std::span<const Node> nodes) {
    return value_or_raise(try_run(nodes));
}
#endif

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
    impl_->globals.push_back({name, std::string(doc)});
    return v;
}

void Context::set_notice_sink(NoticeSink sink, void* user) noexcept {
    impl_->sink = sink;
    impl_->sink_user = user;
}

void Context::notice(Error notice) const {
    if (impl_->sink) impl_->sink(impl_->sink_user, notice);
}

Expected<FuncRef> Context::add(Symbol name, detail::Overload o) {
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
            e.message += "; the script took " + arity_text(n, n) + ", the native takes " +
                         arity_text(o.min_args, o.max_args);
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
    auto r = ctx_->impl().eval(n);
    if (!r) return Unexpected(located(std::move(r).error(), *command_));
    return r;
}

Expected<Node> Args::try_eval(size_t i) const {
    if (i >= size_) return Unexpected(type_error(i, Node(), "value"));
    if (evaluated_) return base_[i];
    return eval_node(base_[i]);
}

Expected<const Array*> Args::try_array(size_t i) const {
    auto r = try_eval(i);
    if (!r) return Unexpected(std::move(r).error());
    if (r->type() != Type::Array) return Unexpected(type_error(i, *r, "array"));
    const Array* a = r->try_array();
    keep(std::move(*r));
    return a;
}

Args Args::rest(size_t i) const noexcept {
    const size_t k = i < size_ ? i : size_;
    return Args(*ctx_, *command_, base_ + k, size_ - k, offset_ + k, evaluated_);
}

Symbol Args::name() const noexcept {
    return command_->empty() ? Symbol() : (*command_)[0].try_sym().value_or(Symbol());
}

Error Args::error(std::string message, ErrorCode code) const {
    return make_error(code, std::move(message), command_->file(), command_->line());
}

Error Args::type_error(size_t i, const Node& n, std::string_view want) const {
    const std::string which =
        "argument " + std::to_string(offset_ + i + 1) + " of " + quoted(name());
    if (i >= size_)
        return error(quoted(name()) + " is missing " + which.substr(0, which.find(" of")));
    return error(which + " is " + std::string(type_name(n.type())) + ", not " + std::string(want));
}

void Args::keep(Node n) const {
    ctx_->impl().kept.push_back(std::move(n));
}

namespace detail {
Expected<Raw<Var>> read_var(const Args& a, size_t i) {
    const Node& n = a.raw(i);
    if (n.type() != Type::Var)
        return Unexpected(a.error("argument " + std::to_string(i + 1) + " of " + quoted(a.name()) +
                                  " must be a $variable, not " + std::string(type_name(n.type()))));
    return Raw<Var>(a.context().impl().cell(n.var_name()), n.var_name());
}
}  // namespace detail

Expected<Node> Raw<Node>::try_eval() const {
    return ctx_->impl().eval(*node_);
}

#if MOIRA_EXCEPTIONS
Node Raw<Node>::eval() const {
    return value_or_raise(try_eval());
}
#endif

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
