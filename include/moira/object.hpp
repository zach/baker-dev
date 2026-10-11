#pragma once

// Objects: C++ types that scripts can call (spec §5, "Objects").
//
//   struct Person : moira::Object<Person> {
//       float height = 6.0f;
//       float on_grow(float amount) { height += amount; return height; }
//       static void bind(moira::Binder<Person>& b) {
//           b.method("grow", &Person::on_grow).doc("Grows by `amount`.");
//           b.property("height", &Person::height);
//           b.method("shrink", [](Person& self, float a) { self.height -= a; });
//       }
//   };
//
//   Person bob;
//   ctx.add(bob, "bob");                 // {bob grow 10} now works from script
//   ctx.call(bob, "grow", 10);           // and the same dispatch from C++
//
// A Context refers to an object by a salted handle (docs/decisions.md D4, D19), so a
// script holding a reference to a destroyed object gets a clean error, never a dangling
// pointer. An object removes itself from its Context when destroyed; a copy of an object
// is not in any Context.
//
// To derive from another bound type, name it as the second argument, so there is one
// ObjectBase: `struct Boss : moira::Object<Boss, Person>`. Boss gets Person's methods and
// properties (copied when Boss's table is built, so dispatch never walks a chain), and a
// method Boss binds replaces Person's of the same name.
//
// A command sent to an object tries, in order: script handlers attached to its type by
// {class} (D50), its native methods, then the methods every object has: `get`, `set`,
// `type`, and `name`. Every object can also carry loose parameters: `set` and `get` reach
// a bound property first, then the parameters.

#include <moira/script.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace moira {

class ObjectBase;

namespace detail {

struct MethodEntry {
    Symbol name;
    std::vector<Overload> overloads;
    std::string doc;
    Status status = Status::Normal;
    Symbol replacement;
    bool inherited = false;
};

struct PropertyEntry {
    Symbol name;
    std::string_view type;
    std::shared_ptr<const void> accessor;
    Node (*get)(const void* accessor, ObjectBase& self) = nullptr;
    bool (*set)(const void* accessor, ObjectBase& self, const Node& value) = nullptr;
    std::string doc;
};

using ObjectParams = std::vector<std::pair<Symbol, Node>>;

// Everything a Context knows about a bound type: built once per type from T::bind.
struct TypeRecord {
    std::string name;
    Symbol symbol;                         // `name`, interned
    std::vector<const TypeRecord*> bases;  // every ancestor, nearest first
    std::vector<MethodEntry> methods;      // sorted by symbol identity, for binary search
    std::vector<PropertyEntry> properties;
    ObjectBase* (*create)() = nullptr;  // for {new}; null if T is not default-constructible
    std::string error;                  // a binding mistake, reported when T is first added

    [[nodiscard]] bool is(const TypeRecord* t) const noexcept {
        return t == this || std::find(bases.begin(), bases.end(), t) != bases.end();
    }
    [[nodiscard]] const MethodEntry* method(Symbol name) const noexcept;
    [[nodiscard]] const PropertyEntry* property(Symbol name) const noexcept;
    void sort();
};

// The unqualified name of T, from the compiler's signature of this function: no RTTI.
template <class T>
std::string type_name_of() {
#if defined(_MSC_VER)
    std::string_view s = __FUNCSIG__;  // ...type_name_of<struct ns::Person>(void)
    s = s.substr(0, s.rfind(">("));
    s = s.substr(s.find("type_name_of<") + 13);
#else
    std::string_view s = __PRETTY_FUNCTION__;  // ...[with T = ns::Person; ...] or [T = ns::Person]
    s = s.substr(s.find("T = ") + 4);
    s = s.substr(0, s.find_first_of(";]"));
#endif
    for (std::string_view prefix : {"struct ", "class "})
        if (s.substr(0, prefix.size()) == prefix) s.remove_prefix(prefix.size());
    const size_t scope = s.substr(0, s.find('<')).rfind("::");
    if (scope != std::string_view::npos) s.remove_prefix(scope + 2);
    return std::string(s);
}

// The object a method is called on: a parameter that takes no argument.
template <class T>
struct Self {
    T* object;
};
template <class T>
struct Param<Self<T>> {
    using Holder = Self<T>;
    static constexpr ParamKind kind = ParamKind::Implicit;
    static constexpr std::string_view type() { return ""; }
    static constexpr bool (*match)(const Node&, bool) noexcept = nullptr;
    static bool read(const Args& a, size_t, std::optional<Holder>& out, Expected<Node>&) {
        out.emplace(Self<T>{static_cast<T*>(a.self())});
        return true;
    }
    static Self<T> pass(Self<T> h) noexcept { return h; }
};

template <class... A>
struct DropFirst;
template <class First, class... Rest>
struct DropFirst<std::tuple<First, Rest...>> {
    using type = std::tuple<Rest...>;
};

// A method as a function taking Self<T> first: from a member function pointer, or from a
// callable whose first parameter is T& (or a base of T).
template <class T, class R, class F, class... A>
auto method_wrapper(F f, std::tuple<A...>*) {
    return [f](Self<T> self, A... a) -> R {
        return std::invoke(f, *self.object, std::forward<A>(a)...);
    };
}

}  // namespace detail

// What a Context stores. Derive from Object<T>, not from this.
class ObjectBase {
public:
    ObjectBase() noexcept = default;
    ObjectBase(const ObjectBase&) noexcept {}  // a copy is not in any Context
    ObjectBase& operator=(const ObjectBase&) noexcept { return *this; }
    virtual ~ObjectBase();

    [[nodiscard]] virtual const detail::TypeRecord& type() const noexcept = 0;
    // An Object node referring to this object, or Int 0 if it is in no Context.
    [[nodiscard]] Node node() const noexcept { return ctx_ ? Node::object(handle_) : Node(); }
    [[nodiscard]] Context* context() const noexcept { return ctx_; }

private:
    friend class Context;
    friend struct Context::Impl;
    Context* ctx_ = nullptr;
    ObjectHandle handle_;
    std::unique_ptr<detail::ObjectParams> params_;  // loose parameters, made on first set
};

template <class T>
class Binder;

template <class T, class Base = ObjectBase>
class Object : public Base {
    static_assert(std::is_base_of_v<ObjectBase, Base>, "Base must be a bound type");

public:
    using Base::Base;
    // T's bindings, built on first use.
    [[nodiscard]] static const detail::TypeRecord& record();
    [[nodiscard]] const detail::TypeRecord& type() const noexcept override { return record(); }
};

// What method() returns: adds documentation and status to the method just bound.
class MethodRef {
public:
    explicit MethodRef(detail::MethodEntry& m) noexcept : m_(&m) {}
    MethodRef& doc(std::string_view text) {
        m_->doc = text;
        return *this;
    }
    MethodRef& status(Status status, Symbol replacement = {}) {
        m_->status = status;
        m_->replacement = replacement;
        return *this;
    }

private:
    detail::MethodEntry* m_;
};

template <class T>
class Binder {
public:
    explicit Binder(detail::TypeRecord& record) noexcept : r_(record) {}

    // The name scripts and messages use for T; by default T's unqualified C++ name.
    void name(std::string_view name) { r_.name = name; }

    // Binds a member function, or a callable whose first parameter is T& (or a base's).
    // Binding a name again adds an overload, as Context::func does.
    template <class F>
    MethodRef method(Symbol name, F f) {
        using Sig = detail::signature_of<F>;
        using R = typename Sig::result;
        auto w = [&] {
            if constexpr (std::is_member_function_pointer_v<F>)
                return detail::method_wrapper<T, R>(f, static_cast<typename Sig::params*>(nullptr));
            else
                return detail::method_wrapper<T, R>(
                    f,
                    static_cast<typename detail::DropFirst<typename Sig::params>::type*>(nullptr));
        }();
        using W = decltype(w);
        detail::Overload o = detail::make_overload<W, R>(
            std::move(w), static_cast<typename detail::signature_of<W>::params*>(nullptr));
        detail::MethodEntry& m = entry(name);
        auto has_raw = [](const detail::Overload& x) {
            return std::any_of(x.params.begin(), x.params.end(),
                               [](const detail::ParamInfo& p) { return p.raw; });
        };
        if (!m.overloads.empty() &&
            (has_raw(o) || std::any_of(m.overloads.begin(), m.overloads.end(), has_raw)))
            r_.error = "method '" + std::string(name.str()) + "' of " + r_.name +
                       " takes unevaluated arguments, so it cannot be overloaded";
        m.overloads.push_back(std::move(o));
        return MethodRef(m);
    }

    // A data member, read and written through Convert<M>.
    template <class M, class C>
    void property(Symbol name, M C::*member, std::string_view doc = {}) {
        static_assert(std::is_base_of_v<C, T>, "the member must belong to T or a base");
        detail::PropertyEntry p;
        p.name = name;
        p.type = detail::Param<M>::type();
        p.doc = doc;
        p.accessor = std::make_shared<M C::*>(member);
        p.get = [](const void* acc, ObjectBase& self) {
            auto r = detail::to_result(static_cast<T&>(self).*(*static_cast<M C::*const*>(acc)));
            return r ? std::move(*r) : Node();
        };
        p.set = [](const void* acc, ObjectBase& self, const Node& v) {
            return Values(std::span<const Node>(&v, 1), nullptr)
                .read(0, static_cast<T&>(self).*(*static_cast<M C::*const*>(acc)));
        };
        add_property(std::move(p));
    }

    // Declares that T derives from bound type B. Object<T, B> does this already; naming it
    // again is harmless.
    template <class B>
    void inherits() {
        static_assert(std::is_base_of_v<B, T> && !std::is_same_v<B, T>, "B must be a base of T");
        inherit(B::record());
    }

    void inherit(const detail::TypeRecord& b) {
        if (r_.is(&b)) return;
        r_.bases.push_back(&b);
        r_.bases.insert(r_.bases.end(), b.bases.begin(), b.bases.end());
        for (detail::MethodEntry m : b.methods) {
            if (std::any_of(r_.methods.begin(), r_.methods.end(),
                            [&](const detail::MethodEntry& x) { return x.name == m.name; }))
                continue;
            m.inherited = true;
            r_.methods.push_back(std::move(m));
        }
        for (const detail::PropertyEntry& p : b.properties)
            if (std::none_of(r_.properties.begin(), r_.properties.end(),
                             [&](const detail::PropertyEntry& x) { return x.name == p.name; }))
                r_.properties.push_back(p);
    }

private:
    detail::MethodEntry& entry(Symbol name) {
        for (detail::MethodEntry& m : r_.methods)
            if (m.name == name) {
                if (m.inherited) m = detail::MethodEntry{name, {}, {}, {}, {}, false};
                return m;
            }
        r_.methods.push_back(detail::MethodEntry{name, {}, {}, {}, {}, false});
        return r_.methods.back();
    }
    void add_property(detail::PropertyEntry p) {
        for (detail::PropertyEntry& x : r_.properties)
            if (x.name == p.name) {
                x = std::move(p);
                return;
            }
        r_.properties.push_back(std::move(p));
    }

    detail::TypeRecord& r_;
};

template <class T, class Base>
const detail::TypeRecord& Object<T, Base>::record() {
    static const detail::TypeRecord r = [] {
        detail::TypeRecord rec;
        rec.name = detail::type_name_of<T>();
        Binder<T> b(rec);
        if constexpr (!std::is_same_v<Base, ObjectBase>) b.inherit(Base::record());
        if constexpr (requires { T::bind(b); }) T::bind(b);
        if constexpr (std::is_default_constructible_v<T>)
            rec.create = []() -> ObjectBase* { return new T(); };
        rec.sort();
        return rec;
    }();
    return r;
}

// --- Context templates. ---

template <class T>
T* Context::try_get(const Node& object) const noexcept {
    ObjectBase* o = try_resolve(object);
    return o && o->type().is(&T::record()) ? static_cast<T*>(o) : nullptr;
}

template <class T>
Expected<T*> Context::get(const Node& object) const {
    if (T* p = try_get<T>(object)) return p;
    return Unexpected(resolve_error(object, T::record().name));
}

template <class T>
void Context::add_type() {
    add_type(T::record());
}

template <class... A>
Expected<Node> Context::call(const ObjectBase& object, Symbol method, A&&... args) {
    const Node nodes[] = {detail::to_node(std::forward<A>(args))..., Node()};
    return call(object.node(), method, std::span<const Node>(nodes, sizeof...(A)));
}

}  // namespace moira
