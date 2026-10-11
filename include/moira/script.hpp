#pragma once

// moira::script: evaluating commands, binding native functions, and variables.
//
// Evaluation is lazy (spec §5). Running {f a b} looks up f's handler and hands it the raw
// command; nothing evaluates `a` or `b` until the handler reads one by type. So `if`,
// `set`, `do`, `and` and `or` are ordinary registered functions, and the evaluator has no
// special forms. A parameter declared Raw<Node> is never evaluated unless the function
// asks; every other parameter is evaluated, then converted through Convert<T>.
//
//   moira::Context ctx;
//   ctx.func("hypot", [](double a, double b) { return std::sqrt(a * a + b * b); });
//   double h = ctx.eval("{hypot 3 4}").as_float();                   // 5
//
// Registering a name again with a different signature adds an overload. Overloads are
// tried in registration order, first on exact node types, then with Int read as Float.
//
// Variables are dynamically scoped, by shallow binding: each $name has one value cell per
// Context, and {do ($a) ...} saves $a, runs the body, and restores it, even on error. A
// function sees, and can change, the variables of whoever called it. That is deliberate:
// it is the original design, and it is why reading $a is a single load. Do not "fix" it
// into lexical scoping.
//
// Typed lookups in moira::data (find_int, get<T>, ...) evaluate a Command or Var value in
// the calling thread's current Context: the innermost Context::Scope, else the Context
// being evaluated, else the first Context created on the thread.
//
// A Context is single-threaded. Separate Contexts may evaluate the same trees at once,
// because evaluation never modifies a tree.

#include <moira/array.hpp>
#include <moira/node.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace moira {

class Context;
class ObjectBase;
namespace detail {
struct TypeRecord;
}

// Where a function comes from (spec §5, "One namespace, three tiers"). Macros are expanded
// by the reader before evaluation, so no function record is ever a macro.
enum class Tier : uint8_t { Native, Script };

// A function's maturity, shown in the reference and the demand report.
enum class Status : uint8_t { Normal, Stub, Provisional, Deprecated };
inline constexpr Status Stub = Status::Stub;
inline constexpr Status Provisional = Status::Provisional;
inline constexpr Status Deprecated = Status::Deprecated;

// The arguments of one call, after the function name.
class Args {
public:
    [[nodiscard]] size_t size() const noexcept { return size_; }
    // Argument `i` (from 0, after the name), raw: never evaluated.
    [[nodiscard]] const Node& raw(size_t i) const noexcept { return base_[i]; }
    // Argument `i`, evaluated.
    [[nodiscard]] Expected<Node> try_eval(size_t i) const;
    // Argument `i`, evaluated, then converted through Convert<T>. A string_view stays valid
    // until the function returns.
    template <class T>
    [[nodiscard]] Expected<T> try_get(size_t i) const;
    // try_get for the binding layer: reads into `out`, or puts the error in `error`.
    template <class T>
    bool read_into(size_t i, T& out, Expected<Node>& error) const;
    // Argument `i`, evaluated, as an array (valid until the function returns).
    [[nodiscard]] Expected<const Array*> try_array(size_t i) const;
#if MOIRA_EXCEPTIONS
    [[nodiscard]] Node eval(size_t i) const { return value(try_eval(i)); }
    template <class T>
    [[nodiscard]] T get(size_t i) const {
        return value(try_get<T>(i));
    }
#endif

    // The arguments from `i` on.
    [[nodiscard]] Args rest(size_t i) const noexcept;
    [[nodiscard]] const Array& command() const noexcept { return *command_; }
    [[nodiscard]] Context& context() const noexcept { return *ctx_; }
    // The function's name, or a method's.
    [[nodiscard]] Symbol name() const noexcept;
    // The object a method was called on; null for a function.
    [[nodiscard]] ObjectBase* self() const noexcept { return self_; }
    // An error naming this function and located at this call.
    [[nodiscard]] Error error(std::string message, ErrorCode code = ErrorCode::Type) const;

    // `first` is the index in `command` of the first argument: 1 for {f ...}, 2 for
    // {object method ...}.
    Args(Context& ctx, const Array& command, const Node* base, size_t size, size_t offset,
         bool evaluated, size_t first = 1, ObjectBase* self = nullptr) noexcept
        : ctx_(&ctx),
          command_(&command),
          base_(base),
          self_(self),
          size_(static_cast<uint32_t>(size)),
          offset_(static_cast<uint32_t>(offset)),
          first_(static_cast<uint8_t>(first)),
          evaluated_(evaluated) {}

private:
    template <class T>
    [[nodiscard]] Expected<T> get_slow(size_t i) const;
    template <class T>
    bool read_slow(size_t i, T& out, Expected<Node>& error) const;
    [[nodiscard]] Expected<Node> eval_node(const Node& n) const;
    void keep(Node n) const;
    [[nodiscard]] Error type_error(size_t i, const Node& n, std::string_view want) const;
#if MOIRA_EXCEPTIONS
    template <class T>
    static T value(Expected<T>&& e) {
        if (!e) detail::raise(std::move(e).error());
        return std::move(*e);
    }
#endif

    Context* ctx_;
    const Array* command_;
    const Node* base_;
    ObjectBase* self_;
    uint32_t size_;
    uint32_t offset_;  // index of base_[0] among all the arguments, for messages
    uint8_t first_;
    bool evaluated_;  // base_ holds values already evaluated (an overload set's arguments)
};

// A parameter that is not evaluated before the call. Raw<Node> is the argument as
// written; Raw<Var> names a variable, for functions like `set`.
template <class T>
class Raw;

template <>
class Raw<Node> {
public:
    Raw(Context& ctx, const Node& node) noexcept : ctx_(&ctx), node_(&node) {}
    [[nodiscard]] const Node& node() const noexcept { return *node_; }
    [[nodiscard]] const Node& operator*() const noexcept { return *node_; }
    [[nodiscard]] const Node* operator->() const noexcept { return node_; }
    [[nodiscard]] Expected<Node> try_eval() const;
#if MOIRA_EXCEPTIONS
    [[nodiscard]] Node eval() const;
#endif

private:
    Context* ctx_;
    const Node* node_;
};

// A variable's value cell. Obtained from Context::var() or a Raw<Var> parameter; stays
// valid for the Context's lifetime.
class VarRef {
public:
    VarRef(Node* cell, Symbol name) noexcept : cell_(cell), name_(name) {}
    [[nodiscard]] Symbol name() const noexcept { return name_; }
    [[nodiscard]] bool is_set() const noexcept;
    // The value; Int 0 if the variable is not set.
    [[nodiscard]] const Node& value() const noexcept;
    [[nodiscard]] const Node* operator->() const noexcept { return &value(); }
    VarRef& operator=(Node v) noexcept {
        *cell_ = std::move(v);
        return *this;
    }
    void unset() noexcept;

private:
    Node* cell_;
    Symbol name_;
};

struct Var {};  // tag for Raw<Var>

// A place to store a value: a $variable, or a [property] of $this (in a method).
template <>
class Raw<Var> {
public:
    Raw(Node* cell, Symbol name) noexcept : cell_(cell), name_(name) {}
    Raw(Context& ctx, const Array& property) noexcept : ctx_(&ctx), property_(&property) {}
    [[nodiscard]] bool is_property() const noexcept { return property_ != nullptr; }
    // The variable's name; empty for a property.
    [[nodiscard]] Symbol name() const noexcept { return name_; }
    // Stores `v`. Setting a property can fail: no $this, or a value of the wrong type.
    [[nodiscard]] Expected<void> try_store(Node v) const;
    // The variable's value (Int 0 if unset); for a property, its value or Int 0.
    [[nodiscard]] Node load() const;

private:
    Node* cell_ = nullptr;
    Symbol name_;
    Context* ctx_ = nullptr;
    const Array* property_ = nullptr;
};

namespace detail {

// Implicit: a parameter that takes no argument (Context&, the object of a method).
enum class ParamKind : uint8_t { Value, Raw, Optional, Rest, Implicit };

struct ParamInfo {
    std::string_view type;  // for the reference and messages
    ParamKind kind;
    // Whether an (evaluated, or literal) argument has the right node type; null: any.
    bool (*match)(const Node& n, bool promote) noexcept;
    bool raw;  // not evaluated before the call (Raw<T>, or an optional one)
};

template <class P>
inline constexpr bool is_raw = false;
template <class T>
inline constexpr bool is_raw<Raw<T>> = true;
template <class T>
inline constexpr bool is_raw<std::optional<Raw<T>>> = true;

// One signature of a function.
struct Overload {
    using Thunk = Expected<Node> (*)(const void* callable, Args& args);
    Thunk thunk = nullptr;
    std::shared_ptr<const void> callable;
    std::vector<ParamInfo> params;  // excluding implicit ones
    std::string_view result;
    uint32_t min_args = 0;
    uint32_t max_args = 0;  // UINT32_MAX: takes the rest
};

// --- Signature deduction. ---

template <class F>
struct signature_of : signature_of<decltype(&std::remove_cvref_t<F>::operator())> {};
template <class R, class... A>
struct signature_of<R (*)(A...)> {
    using result = R;
    using params = std::tuple<A...>;
};
template <class R, class... A>
struct signature_of<R (*)(A...) noexcept> : signature_of<R (*)(A...)> {};
template <class C, class R, class... A>
struct signature_of<R (C::*)(A...) const> : signature_of<R (*)(A...)> {};
template <class C, class R, class... A>
struct signature_of<R (C::*)(A...) const noexcept> : signature_of<R (*)(A...)> {};
template <class C, class R, class... A>
struct signature_of<R (C::*)(A...)> : signature_of<R (*)(A...)> {};
template <class C, class R, class... A>
struct signature_of<R (C::*)(A...) noexcept> : signature_of<R (*)(A...)> {};

template <class T>
bool match_value(const Node& n, bool promote) noexcept {
    if constexpr (std::is_same_v<T, bool> || std::is_integral_v<T>) {
        return n.type() == Type::Int;
    } else if constexpr (std::is_floating_point_v<T>) {
        return n.type() == Type::Float || (promote && n.type() == Type::Int);
    } else if constexpr (std::is_same_v<T, Symbol>) {
        return n.type() == Type::Symbol;
    } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
        return n.type() == Type::String;
    } else if constexpr (std::is_same_v<T, Node>) {
        return true;
    } else {
        T out{};
        return Values(std::span<const Node>(&n, 1), nullptr).read(0, out);
    }
}

// Moves a read's value into `out`, or its error into `result`.
template <class E, class H>
bool take(E&& r, std::optional<H>& out, Expected<Node>& result) {
    if (!r) {
        result = Unexpected(std::move(r).error());
        return false;
    }
    out.emplace(std::move(*r));
    return true;
}

// How a parameter of type P reads its argument. `Holder` is what is read; pass() turns it
// into the argument.
template <class P>
struct Param {
    using T = std::remove_cvref_t<P>;
    using Holder = T;
    static constexpr ParamKind kind = ParamKind::Value;
    static constexpr std::string_view type() {
        if constexpr (std::is_same_v<T, Node>)
            return "value";
        else
            return convert_info<T>().name;
    }
    static constexpr auto match = &match_value<T>;
    static bool read(const Args& a, size_t i, std::optional<T>& out, Expected<Node>& result) {
        return a.read_into(i, out.emplace(), result);
    }
    static T&& pass(T& h) noexcept { return std::move(h); }
};
template <>
struct Param<const Array&> {
    using Holder = const Array*;
    static constexpr ParamKind kind = ParamKind::Value;
    static constexpr std::string_view type() { return "array"; }
    static bool match_array(const Node& n, bool) noexcept { return n.type() == Type::Array; }
    static constexpr auto match = &match_array;
    static bool read(const Args& a, size_t i, std::optional<Holder>& out, Expected<Node>& result) {
        return take(a.try_array(i), out, result);
    }
    static const Array& pass(const Array* h) noexcept { return *h; }
};
template <>
struct Param<Raw<Node>> {
    using Holder = Raw<Node>;
    static constexpr ParamKind kind = ParamKind::Raw;
    static constexpr std::string_view type() { return "unevaluated"; }
    static constexpr bool (*match)(const Node&, bool) noexcept = nullptr;
    static bool read(const Args& a, size_t i, std::optional<Holder>& out, Expected<Node>&) {
        out.emplace(a.context(), a.raw(i));
        return true;
    }
    static Raw<Node> pass(Raw<Node>& h) noexcept { return h; }
};
Expected<Raw<Var>> read_var(const Args& a, size_t i);
template <>
struct Param<Raw<Var>> {
    using Holder = Raw<Var>;
    static constexpr ParamKind kind = ParamKind::Raw;
    static constexpr std::string_view type() { return "$variable"; }
    static bool match_var(const Node& n, bool) noexcept {
        return n.type() == Type::Var || n.type() == Type::Property;
    }
    static constexpr auto match = &match_var;
    static bool read(const Args& a, size_t i, std::optional<Holder>& out, Expected<Node>& result) {
        return take(read_var(a, i), out, result);
    }
    static Raw<Var> pass(Raw<Var>& h) noexcept { return h; }
};
template <class T>
struct Param<std::optional<T>> {
    using Holder = std::optional<T>;
    static constexpr ParamKind kind = ParamKind::Optional;
    static constexpr std::string_view type() { return Param<T>::type(); }
    static constexpr auto match = Param<T>::match;
    static bool read(const Args& a, size_t i, std::optional<Holder>& out, Expected<Node>& result) {
        if (i >= a.size()) {
            out.emplace();
            return true;
        }
        std::optional<typename Param<T>::Holder> inner;
        if (!Param<T>::read(a, i, inner, result)) return false;
        out.emplace(std::move(*inner));
        return true;
    }
    static std::optional<T>&& pass(std::optional<T>& h) noexcept { return std::move(h); }
};
template <>
struct Param<Args> {
    using Holder = Args;
    static constexpr ParamKind kind = ParamKind::Rest;
    static constexpr std::string_view type() { return "values..."; }
    static constexpr bool (*match)(const Node&, bool) noexcept = nullptr;
    static bool read(const Args& a, size_t i, std::optional<Holder>& out, Expected<Node>&) {
        out.emplace(a.rest(i));
        return true;
    }
    static Args pass(Args& h) noexcept { return h; }
};
template <>
struct Param<Context&> {
    using Holder = Context*;
    static constexpr ParamKind kind = ParamKind::Implicit;
    static constexpr std::string_view type() { return ""; }
    static constexpr bool (*match)(const Node&, bool) noexcept = nullptr;
    static bool read(const Args& a, size_t, std::optional<Holder>& out, Expected<Node>&) {
        out.emplace(&a.context());
        return true;
    }
    static Context& pass(Context* h) noexcept { return *h; }
};

template <class T>
struct is_expected : std::false_type {};
template <class U>
struct is_expected<expected<U, Error>> : std::true_type {};

// The return value as a Node.
template <class R>
Expected<Node> to_result(R&& r) {
    using T = std::remove_cvref_t<R>;
    if constexpr (std::is_same_v<T, Node>) {
        return std::forward<R>(r);
    } else if constexpr (is_expected<T>::value) {
        if (!r) return Unexpected(std::forward<R>(r).error());
        if constexpr (std::is_void_v<typename T::value_type>)
            return Node();
        else
            return to_result(*std::forward<R>(r));
    } else if constexpr (std::is_same_v<T, bool>) {
        return Node(r ? 1 : 0);
    } else if constexpr (std::is_same_v<T, float>) {
        return Node(float_to_decimal_double(r));
    } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, Symbol>) {
        return Node(r);
    } else if constexpr (std::is_same_v<T, Ref<Array>>) {
        return Node(std::forward<R>(r));
    } else {
        ValueList out;
        Convert<T>::to(r, out);
        if (out.nodes().size() == 1) return std::move(out.nodes()[0]);
        return Node(Array::make_from(out.nodes()));
    }
}

template <class R>
constexpr std::string_view result_name() {
    using T = std::remove_cvref_t<R>;
    if constexpr (std::is_void_v<T>) {
        return "nothing";
    } else if constexpr (std::is_same_v<T, Node>) {
        return "value";
    } else if constexpr (std::is_same_v<T, Ref<Array>>) {
        return "array";
    } else if constexpr (is_expected<T>::value) {
        return result_name<typename T::value_type>();
    } else {
        return convert_info<T>().name;
    }
}

// The argument index of parameter `k`: a Context& parameter takes no argument.
template <class... P>
struct ArgIndex {
    static constexpr size_t of(size_t k) {
        constexpr bool takes[] = {(Param<P>::kind != ParamKind::Implicit)..., false};
        size_t n = 0;
        for (size_t j = 0; j < k; ++j) n += takes[j] ? 1 : 0;
        return n;
    }
};

template <class F, class R, class... P, size_t... I>
Expected<Node> invoke(const void* callable, [[maybe_unused]] Args& args,
                      std::index_sequence<I...>) {
    // One Expected for the whole call, holding a read error or the result: frames here
    // nest once per script call, so they stay small.
    Expected<Node> result;
    [[maybe_unused]] std::tuple<std::optional<typename Param<P>::Holder>...> held;
    bool ok = true;
    if constexpr (sizeof...(P) > 0) {
        using Index = ArgIndex<P...>;
        ok = (Param<P>::read(args, Index::of(I), std::get<I>(held), result) && ...);
    }
    if (ok) {
        const F& f = *static_cast<const F*>(callable);
        if constexpr (std::is_void_v<R>)
            std::invoke(f, Param<P>::pass(*std::get<I>(held))...);
        else
            result = to_result(std::invoke(f, Param<P>::pass(*std::get<I>(held))...));
    }
    return result;
}

template <class F, class R, class... P>
Overload make_overload(F&& f, std::tuple<P...>*) {
    using Fn = std::remove_cvref_t<F>;
    Overload o;
    o.thunk = [](const void* c, Args& a) {
        return invoke<Fn, R, P...>(c, a, std::index_sequence_for<P...>{});
    };
    o.callable = std::make_shared<const Fn>(std::forward<F>(f));
    o.result = result_name<R>();
    (
        [&] {
            if constexpr (Param<P>::kind != ParamKind::Implicit)
                o.params.push_back({Param<P>::type(), Param<P>::kind, Param<P>::match, is_raw<P>});
        }(),
        ...);
    for (const ParamInfo& p : o.params) {
        if (p.kind == ParamKind::Rest) {
            o.max_args = UINT32_MAX;
            break;
        }
        ++o.max_args;
        if (p.kind != ParamKind::Optional) o.min_args = o.max_args;
    }
    return o;
}

}  // namespace detail

// What func() returns: adds documentation and status to the function just registered.
class FuncRef {
public:
    explicit FuncRef(void* function) noexcept : f_(function) {}
    FuncRef& doc(std::string_view text);
    // `replacement` names what to use instead of a Deprecated function.
    FuncRef& status(Status status, Symbol replacement = {});

private:
    void* f_;
};

// The result of moira::check (below).
struct CheckReport {
    struct Site {
        Symbol file;
        int32_t line = 0;
    };
    // The push-up backlog (spec §5): what the data asks for that nothing provides yet.
    struct Demand {
        Symbol name;
        bool stub = false;  // a call to a function whose status is Stub; else unresolved
        std::vector<Site> sites;
        Symbol on;  // for a method: the class of the object it was sent to
    };
    std::vector<Error> errors;
    std::vector<Demand> demand;  // in order of first appearance

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    // Errors first, then the demand report, one line each.
    [[nodiscard]] std::string to_string() const;
};

// A call in progress, on the C++ stack: the frames of a Context form a list from the
// innermost call out (Context::top_frame). Read-only, and valid only while the call runs.
struct CallFrame {
    const Array* command;  // the call as written: its head, file, and line
    const CallFrame* parent;
};

class Context {
public:
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // --- Evaluation. ---

    // A Command runs its function, a Var reads its variable, anything else is itself.
    [[nodiscard]] Expected<Node> try_eval(const Node& node);
    // Parses `text` and evaluates each top-level node in order, returning the last value
    // (Int 0 if there is none). The entry point for consoles and remote commands.
    [[nodiscard]] Expected<Node> try_eval(std::string_view text, std::string_view name = "<eval>");
    // Evaluates each node of `nodes` in order and returns the last value: runs a script.
    [[nodiscard]] Expected<Node> try_run(std::span<const Node> nodes);
#if MOIRA_EXCEPTIONS
    Node eval(const Node& node);
    Node eval(std::string_view text, std::string_view name = "<eval>");
    Node run(std::span<const Node> nodes);
#endif

    // --- Functions. ---

    // Registers `f` under `name`. Parameter and return types are deduced from its
    // signature (spec §5, "Native function binding"); a registration that would make an
    // overload set ambiguous is an error.
    template <class F>
    [[nodiscard]] Expected<FuncRef> try_func(Symbol name, F&& f) {
        using Sig = detail::signature_of<F>;
        return add(name, detail::make_overload<F, typename Sig::result>(
                             std::forward<F>(f), static_cast<typename Sig::params*>(nullptr)));
    }
#if MOIRA_EXCEPTIONS
    template <class F>
    FuncRef func(Symbol name, F&& f) {
        auto r = try_func(name, std::forward<F>(f));
        if (!r) detail::raise(std::move(r).error());
        return *r;
    }
#endif
    [[nodiscard]] bool has_func(Symbol name) const noexcept;

    // --- Variables. ---

    // The variable `name`, created unset if new.
    [[nodiscard]] VarRef var(Symbol name);
    // A documented global: sets `name` to `initial` and lists it in the reference.
    VarRef global(Symbol name, Node initial, std::string_view doc = {});

    // --- Objects (object.hpp: Object<T>, Binder<T>). ---

    // Adds `object`, named `name` if given, and returns an Object node referring to it. The
    // object removes itself when destroyed. A name must not be a function's or another
    // object's; an object can be in one Context at a time.
    [[nodiscard]] Expected<Node> try_add(ObjectBase& object, Symbol name = {});
    void remove(ObjectBase& object) noexcept;
    // The object `object` (an Object node) refers to; null if it was removed (weak).
    [[nodiscard]] ObjectBase* try_resolve(const Node& object) const noexcept;
    // As try_resolve, and of type T or derived from it.
    template <class T>
    [[nodiscard]] T* try_get(const Node& object) const noexcept;
    // The object named `name`, as an Object node; Int 0 if there is none.
    [[nodiscard]] Node object(Symbol name) const noexcept;
    // Makes T known before any T is added: for {class T ...}, {new T}, and the reference.
    template <class T>
    void add_type();
    // Sends {object method args...}: script handlers, then native methods, then the
    // methods every object has (object.hpp).
    [[nodiscard]] Expected<Node> try_call(const Node& object, Symbol method,
                                          std::span<const Node> args = {});
    template <class... A>
    [[nodiscard]] Expected<Node> try_call(const ObjectBase& object, Symbol method, A&&... args);
#if MOIRA_EXCEPTIONS
    Node add(ObjectBase& object, Symbol name = {});
    // Strong resolution: throws if the object was removed or is not a T.
    template <class T>
    T& get(const Node& object) const;
    template <class... A>
    Node call(const ObjectBase& object, Symbol method, A&&... args);
#endif

    // Nesting deeper than 1,000 calls, or than `bytes` of stack below the outermost call, is
    // a Limit error rather than a stack overflow. The default, 256 KB, is safe on any common
    // thread; release builds nest about 400 calls in it, debug builds fewer. Raise it on a
    // thread with a larger stack.
    void set_stack_budget(size_t bytes) noexcept;

    // --- Debugging. ---

    // The innermost call in progress; null when nothing is running. Follow `parent` out.
    [[nodiscard]] const CallFrame* top_frame() const noexcept;
    // The calls in progress, innermost first, as an error's stack records them.
    [[nodiscard]] std::vector<StackEntry> backtrace() const;
    [[nodiscard]] StackEntry stack_entry(const CallFrame& frame) const;

    // --- Notices: things worth saying that are not errors, such as a native function
    // replacing a script one. By default they go to stderr. ---
    using NoticeSink = void (*)(void* user, const Error& notice);
    void set_notice_sink(NoticeSink sink, void* user) noexcept;
    void notice(Error notice) const;

    // Makes a Context current on this thread for typed lookups in moira::data.
    class Scope {
    public:
        explicit Scope(Context& ctx) noexcept;
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Context* previous_;
    };
    [[nodiscard]] static Context* current() noexcept;

    struct Impl;
    [[nodiscard]] Impl& impl() const noexcept { return *impl_; }

private:
    [[nodiscard]] Expected<FuncRef> add(Symbol name, detail::Overload overload);
    void add_type(const detail::TypeRecord& type);
    [[nodiscard]] Error resolve_error(const Node& object, std::string_view type) const;

    std::unique_ptr<Impl> impl_;
};

// The script calls in progress on this thread's current Context (Context::current), one
// line each, innermost first; empty if there are none. For an assert handler or crash
// reporter to print beside the C++ stack.
[[nodiscard]] std::string script_backtrace();

// Walks `tree` without evaluating anything and reports, located, every call to a
// registered function with the wrong number of arguments for every overload, or a literal
// argument of the wrong type where the signature fixes one; plus the demand report. Calls
// to functions defined by {func} in the checked tree count as registered. Unregistered
// heads, arguments that are commands or variables, and unevaluated or rest parameters are
// not checked: their types are unknown until run. Accumulates into `report`, so one
// report can cover many files.
void check(const Array& tree, const Context& ctx, CheckReport& report);
[[nodiscard]] CheckReport check(const Array& tree, const Context& ctx);

// The script reference (spec §5): every function with its overloads, tier, status, and
// documentation, and every documented global, as Markdown.
[[nodiscard]] std::string reference(const Context& ctx);
[[nodiscard]] Expected<void> try_write_reference(const Context& ctx, std::string_view path);

// --- Template definitions. ---

template <class T>
Expected<T> Args::try_get(size_t i) const {
    // The common case: a literal that converts.
    if (i < size_) {
        const Node& n = base_[i];
        if (evaluated_ || (n.type() != Type::Command && n.type() != Type::Var)) {
            T out{};
            if (Values(std::span<const Node>(&n, 1), command_).read(0, out)) return out;
        }
    }
    return get_slow<T>(i);
}

template <class T>
bool Args::read_into(size_t i, T& out, Expected<Node>& error) const {
    if (i < size_) {
        const Node& n = base_[i];
        if (evaluated_ || (n.type() != Type::Command && n.type() != Type::Var))
            if (Values(std::span<const Node>(&n, 1), command_).read(0, out)) return true;
    }
    return read_slow(i, out, error);
}

template <class T>
bool Args::read_slow(size_t i, T& out, Expected<Node>& error) const {
    auto r = get_slow<T>(i);
    if (!r) {
        error = Unexpected(std::move(r).error());
        return false;
    }
    out = std::move(*r);
    return true;
}

template <class T>
Expected<T> Args::get_slow(size_t i) const {
    static constexpr detail::ConvertInfo kInfo = detail::convert_info<T>();
    if (i >= size_) return Unexpected(type_error(i, Node(), kInfo.name));
    const Node* n = base_ + i;
    Node value;
    if (!evaluated_ && (n->type() == Type::Command || n->type() == Type::Var)) {
        auto r = eval_node(*n);
        if (!r) return Unexpected(std::move(r).error());
        value = std::move(*r);
        n = &value;
    }
    T out{};
    if (!Values(std::span<const Node>(n, 1), command_).read(0, out))
        return Unexpected(type_error(i, *n, std::is_same_v<T, Node> ? "value" : kInfo.name));
    if constexpr (std::is_same_v<T, std::string_view>)
        if (n == &value) keep(std::move(value));
    return out;
}

}  // namespace moira
