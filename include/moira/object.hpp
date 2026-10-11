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

#include <string_view>
#include <type_traits>
#include <utility>

namespace moira {

class ObjectBase;

namespace detail {

// What a Context knows about a bound type: built once per type from T::bind. Its layout
// is private to moira::script (internal.hpp); a binding reaches it through the functions
// below, called once per type.
struct TypeRecord;
struct MethodEntry;
struct ObjectParams;

[[nodiscard]] TypeRecord* new_type_record(std::string_view name);
void set_type_name(TypeRecord& type, std::string_view name);
// Adds an overload to method `name`, replacing an inherited method of that name.
[[nodiscard]] MethodEntry* add_method(TypeRecord& type, Symbol name, Overload overload);
void set_method_doc(MethodEntry& method, std::string_view doc);
void set_method_status(MethodEntry& method, Status status, Symbol replacement);
using PropertyGet = Node (*)(const void* accessor, ObjectBase& self);
using PropertySet = bool (*)(const void* accessor, ObjectBase& self, const Node& value);
void add_property(TypeRecord& type, Symbol name, std::string_view value_type, Shared accessor,
                  PropertyGet get, PropertySet set, std::string_view doc);
void inherit(TypeRecord& type, const TypeRecord& base);
// {new T} and its delete, through the creating Context's allocator.
using CreateObject = ObjectBase* (*)(const Allocator& allocator);
using DestroyObject = void (*)(const Allocator& allocator, ObjectBase* object);
void set_create(TypeRecord& type, CreateObject create, DestroyObject destroy);
void finish_type_record(TypeRecord& type);
// Whether `type` is `other` or derives from it.
[[nodiscard]] bool type_is(const TypeRecord& type, const TypeRecord* other) noexcept;
[[nodiscard]] std::string_view type_record_name(const TypeRecord& type) noexcept;

// The unqualified name of T, from the compiler's signature of this function: no RTTI. The
// view points into that signature, which is static.
template <class T>
std::string_view type_name_of() {
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
    return s;
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

template <class L>
struct DropFirst;
template <class First, class... Rest>
struct DropFirst<TypeList<First, Rest...>> {
    using type = TypeList<Rest...>;
};

// A method as a function taking Self<T> first: from a member function pointer, or from a
// callable whose first parameter is T& (or a base of T).
template <class T, class R, class F, class... A>
auto method_wrapper(F f, TypeList<A...>*) {
    return [f](Self<T> self, A... a) -> R {
        if constexpr (std::is_member_function_pointer_v<F>)
            return (self.object->*f)(std::forward<A>(a)...);
        else
            return f(*self.object, std::forward<A>(a)...);
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
    // The name scripts use for the object's type: "Person".
    [[nodiscard]] std::string_view type_name() const noexcept {
        return detail::type_record_name(type());
    }
    // An Object node referring to this object, or Int 0 if it is in no Context.
    [[nodiscard]] Node node() const noexcept { return ctx_ ? Node::object(handle_) : Node(); }
    [[nodiscard]] Context* context() const noexcept { return ctx_; }

private:
    friend class Context;
    friend struct Context::Impl;
    Context* ctx_ = nullptr;
    ObjectHandle handle_;
    detail::ObjectParams* params_ = nullptr;  // loose parameters, made on first set
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
        detail::set_method_doc(*m_, text);
        return *this;
    }
    MethodRef& status(Status status, Symbol replacement = {}) {
        detail::set_method_status(*m_, status, replacement);
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
    void name(std::string_view name) { detail::set_type_name(r_, name); }

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
        return MethodRef(*detail::add_method(
            r_, name,
            detail::make_overload<W, R>(
                std::move(w), static_cast<typename detail::signature_of<W>::params*>(nullptr))));
    }

    // A data member, read and written through Convert<M>.
    template <class M, class C>
    void property(Symbol name, M C::*member, std::string_view doc = {}) {
        static_assert(std::is_base_of_v<C, T>, "the member must belong to T or a base");
        detail::add_property(
            r_, name, detail::Param<M>::type(), detail::Shared::make<M C::*>(member),
            [](const void* acc, ObjectBase& self) {
                auto r =
                    detail::to_result(static_cast<T&>(self).*(*static_cast<M C::*const*>(acc)));
                return r ? std::move(*r) : Node();
            },
            [](const void* acc, ObjectBase& self, const Node& v) {
                return Values(std::span<const Node>(&v, 1), nullptr)
                    .read(0, static_cast<T&>(self).*(*static_cast<M C::*const*>(acc)));
            },
            doc);
    }

    // Declares that T derives from bound type B. Object<T, B> does this already; naming it
    // again is harmless.
    template <class B>
    void inherits() {
        static_assert(std::is_base_of_v<B, T> && !std::is_same_v<B, T>, "B must be a base of T");
        inherit(B::record());
    }

    void inherit(const detail::TypeRecord& b) { detail::inherit(r_, b); }

private:
    detail::TypeRecord& r_;
};

template <class T, class Base>
const detail::TypeRecord& Object<T, Base>::record() {
    static const detail::TypeRecord* const r = [] {
        detail::TypeRecord& rec = *detail::new_type_record(detail::type_name_of<T>());
        Binder<T> b(rec);
        if constexpr (!std::is_same_v<Base, ObjectBase>) b.inherit(Base::record());
        if constexpr (requires { T::bind(b); }) T::bind(b);
        if constexpr (std::is_default_constructible_v<T>)
            detail::set_create(
                rec, [](const Allocator& a) -> ObjectBase* { return detail::create<T>(a); },
                [](const Allocator& a, ObjectBase* o) { detail::destroy(a, static_cast<T*>(o)); });
        detail::finish_type_record(rec);
        return &rec;
    }();
    return *r;
}

// --- Context templates. ---

template <class T>
T* Context::try_get(const Node& object) const noexcept {
    ObjectBase* o = try_resolve(object);
    if (!o) return nullptr;
    const detail::TypeRecord& t = o->type();
    return &t == &T::record() || detail::type_is(t, &T::record()) ? static_cast<T*>(o) : nullptr;
}

template <class T>
Expected<T*> Context::get(const Node& object) const {
    if (T* p = try_get<T>(object)) return p;
    return Unexpected(resolve_error(object, detail::type_record_name(T::record())));
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
