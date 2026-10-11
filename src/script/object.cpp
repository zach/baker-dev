// Objects: the per-Context object table, method dispatch, properties and loose parameters,
// and the {class}, {new} and {delete} built-ins.
#include "internal.hpp"

#include <functional>

namespace moira {

using detail::ObjectParams;

namespace {

std::string quoted(Symbol s) {
    return "'" + std::string(s.str()) + "'";
}

#if defined(_MSC_VER)
#define MOIRA_COLD __declspec(noinline)
#else
#define MOIRA_COLD [[gnu::noinline, gnu::cold]]
#endif

MOIRA_COLD Expected<Node> fail(const Array& at, ErrorCode code, std::string message) {
    Error e = make_error(code, std::move(message), at.file(), at.line());
    record_stack(e);
    return Unexpected(std::move(e));
}

// An instance of a script-only class.
struct ScriptObject final : Object<ScriptObject> {};

const Symbol& sym_this() {
    static const Symbol s("this");
    return s;
}

// A bound property, else a loose parameter. Null if neither exists.
std::optional<Node> get_property(ObjectBase& o, Symbol name, ObjectParams* params) {
    if (const detail::PropertyEntry* p = o.type().property(name))
        return p->get(p->accessor.get(), o);
    if (params)
        for (const auto& [k, v] : *params)
            if (k == name) return v;
    return std::nullopt;
}

}  // namespace

// --- TypeRecord ---

namespace detail {

const MethodEntry* TypeRecord::method(Symbol method_name) const noexcept {
    auto it = std::lower_bound(
        methods.begin(), methods.end(), method_name,
        [](const MethodEntry& m, Symbol s) { return std::less<>()(m.name.id(), s.id()); });
    return it != methods.end() && it->name == method_name ? &*it : nullptr;
}

const PropertyEntry* TypeRecord::property(Symbol property_name) const noexcept {
    for (const PropertyEntry& p : properties)
        if (p.name == property_name) return &p;
    return nullptr;
}

void TypeRecord::sort() {
    std::sort(methods.begin(), methods.end(), [](const MethodEntry& a, const MethodEntry& b) {
        return std::less<>()(a.name.id(), b.name.id());
    });
    symbol = Symbol(name);
}

}  // namespace detail

ObjectBase::~ObjectBase() {
    if (ctx_) ctx_->remove(*this);
}

// --- Impl ---

Expected<Node> Context::Impl::add(ObjectBase& object, Symbol name, ScriptClass* script,
                                  bool owned) {
    const detail::TypeRecord& type = object.type();
    if (!type.error.empty()) return Unexpected(make_error(ErrorCode::Refused, type.error));
    if (object.ctx_)
        return Unexpected(make_error(ErrorCode::Refused, "the object is already in a Context"));
    if (!name.empty()) {
        if (const Function* f = find(name); f && !f->overloads.empty())
            return Unexpected(make_error(
                ErrorCode::Refused, quoted(name) + " is a function, so it cannot name an object"));
        if (slot(named(name)))
            return Unexpected(
                make_error(ErrorCode::Refused, "an object is already named " + quoted(name)));
    }
    uint32_t index;
    if (!free_slots.empty()) {
        index = free_slots.back();
        free_slots.pop_back();
    } else {
        index = static_cast<uint32_t>(slots.size());
        slots.emplace_back();
    }
    Slot& s = slots[index];
    s.object = &object;
    s.name = name;
    s.script = script;
    s.owned = owned;
    object.ctx_ = &self;
    object.handle_ = ObjectHandle::make(index, s.salt);
    if (!name.empty()) {
        if (name.index() >= by_name.size()) by_name.resize(name.index() + 1);
        by_name[name.index()] = object.handle_;
    }
    if (!script && std::find(types.begin(), types.end(), &type) == types.end())
        types.push_back(&type);
    return Node::object(object.handle_);
}

void Context::Impl::remove(ObjectHandle h) noexcept {
    Slot* s = slot(h);
    if (!s) return;
    if (!s->name.empty() && s->name.index() < by_name.size()) by_name[s->name.index()] = {};
    s->object->ctx_ = nullptr;
    *s = Slot{nullptr, s->salt + 1 == 0 ? 1 : s->salt + 1, {}, nullptr, false};
    free_slots.push_back(h.index());
}

ScriptClass* Context::Impl::script_class(Symbol name) const noexcept {
    const uint32_t i = name.index();
    return i < class_by_name.size() ? class_by_name[i] : nullptr;
}

const detail::TypeRecord* Context::Impl::type_named(Symbol name) const noexcept {
    for (const detail::TypeRecord* t : types)
        if (t->symbol == name) return t;
    return nullptr;
}

std::string Context::Impl::class_name(const Slot& s) const {
    return s.script ? std::string(s.script->name.str()) : s.object->type().name;
}

// {object method args...}: script handlers, then native methods, then the methods every
// object has.
Expected<Node> Context::Impl::send(ObjectHandle target, const Array& command) {
    Slot* s = slot(target);
    if (!s)
        return fail(command, ErrorCode::NotFound,
                    "the object this command is sent to was removed (a stale handle)");
    if (command.size() < 2 || command[1].type() != Type::Symbol)
        return fail(command, ErrorCode::Syntax,
                    "an object command names a method: {object method ...}");
    const Symbol method = *command[1].try_sym();
    ObjectBase* object = s->object;
    const detail::TypeRecord& type = object->type();
    const size_t n = command.size() - 2;

    // Script handlers: the object's own class, else classes named after its type or an
    // ancestor's, most derived first.
    const ScriptClass::Handler* handler = nullptr;
    if (s->script) {
        handler = s->script->handler(method);
    } else if (!classes.empty()) {
        for (size_t i = 0; !handler && i <= type.bases.size(); ++i) {
            const detail::TypeRecord& t = i == 0 ? type : *type.bases[i - 1];
            if (const ScriptClass* c = script_class(t.symbol)) handler = c->handler(method);
        }
    }
    if (handler) {
        if (n != handler->body.params.size())
            return fail(command, ErrorCode::Type,
                        quoted(method) + " takes " +
                            arity_text(uint32_t(handler->body.params.size()),
                                       uint32_t(handler->body.params.size())) +
                            ", given " + std::to_string(n));
        Args args(self, command, command.begin() + 2, n, 0, false, 2, object);
        const Node this_node = Node::object(target);
        Expected<Node> r = run_script(handler->body, args, &this_node);
        if (!r && r.error().file.empty()) {
            r.error().file = command.file();
            r.error().line = command.line();
        }
        return r;
    }
    if (const detail::MethodEntry* m = type.method(method))
        return invoke_set(method, m->overloads, command, 2, object);

    // The methods every object has.
    static const Symbol kGet("get"), kSet("set"), kType("type"), kName("name");
    Args args(self, command, command.begin() + 2, n, 0, false, 2, object);
    if (method == kType || method == kName) {
        if (n != 0) return fail(command, ErrorCode::Type, quoted(method) + " takes no arguments");
        if (method == kType) return Node(Symbol(class_name(*s)));
        return s->name.empty() ? Node() : Node(s->name);
    }
    if (method == kGet || method == kSet) {
        if (n != (method == kGet ? 1u : 2u))
            return fail(command, ErrorCode::Type,
                        quoted(method) + " takes " +
                            arity_text(method == kGet ? 1 : 2, method == kGet ? 1 : 2) +
                            ", given " + std::to_string(n));
        auto key = args.try_get<Symbol>(0);
        if (!key) return Unexpected(std::move(key).error());
        if (method == kGet) {
            if (auto v = get_property(*object, *key, object->params_.get())) return *v;
            return fail(command, ErrorCode::NotFound,
                        class_name(*s) + " has no property or parameter " + quoted(*key));
        }
        auto value = args.try_eval(1);
        if (!value) return value;
        auto r = set_property(*object, *key, *value, command);
        if (!r) return Unexpected(std::move(r).error());
        return value;
    }
    return fail(command, ErrorCode::NotFound, class_name(*s) + " has no method " + quoted(method));
}

Expected<void> Context::Impl::set_property(ObjectBase& o, Symbol name, const Node& value,
                                           const Array& at) {
    if (const detail::PropertyEntry* p = o.type().property(name)) {
        if (p->set(p->accessor.get(), o, value)) return {};
        return Unexpected(make_error(ErrorCode::Type,
                                     "property " + quoted(name) + " of " + o.type().name + " is " +
                                         std::string(p->type) + ", not " +
                                         std::string(moira::type_name(value.type())),
                                     at.file(), at.line()));
    }
    if (!o.params_) o.params_ = std::make_unique<ObjectParams>();
    for (auto& [k, v] : *o.params_)
        if (k == name) {
            v = value;
            return {};
        }
    o.params_->emplace_back(name, value);
    return {};
}

// [a b ...]: property `a` of $this, then `b` of that (an object's property, or the value
// tagged `b` in an array), and so on.
Expected<Node> Context::Impl::property_path(const Array& path) {
    const Node* self_node = cell_if(sym_this());
    Node current = self_node ? *self_node : Node();
    if (current.type() != Type::Object)
        return fail(path, ErrorCode::NotFound, "[...] reads a property of $this, which is not set");
    for (const Node& seg : path.nodes()) {
        const auto name = seg.try_sym();
        if (!name) return fail(path, ErrorCode::Syntax, "[...] holds property names");
        if (current.type() == Type::Object) {
            Slot* s = slot(current.object_handle());
            if (!s) return fail(path, ErrorCode::NotFound, "[...] reads a removed object");
            auto v = get_property(*s->object, *name, s->object->params_.get());
            if (!v)
                return fail(path, ErrorCode::NotFound,
                            class_name(*s) + " has no property or parameter " + quoted(*name));
            current = std::move(*v);
        } else if (const Array* a = current.try_array(); a && a->try_find(*name)) {
            const Array& tagged = *a->try_find(*name);
            current = tagged.size() > 1 ? tagged[1] : Node();
        } else {
            return fail(path, ErrorCode::NotFound, quoted(*name) + " is not found in the value");
        }
    }
    return current;
}

// --- Context ---

Expected<Node> Context::try_add(ObjectBase& object, Symbol name) {
    return impl_->add(object, name, nullptr, false);
}

void Context::remove(ObjectBase& object) noexcept {
    if (object.ctx_ == this) impl_->remove(object.handle_);
}

ObjectBase* Context::try_resolve(const Node& object) const noexcept {
    if (object.type() != Type::Object) return nullptr;
    Impl::Slot* s = impl_->slot(object.object_handle());
    return s ? s->object : nullptr;
}

Node Context::object(Symbol name) const noexcept {
    const ObjectHandle h = impl_->named(name);
    return impl_->slot(h) ? Node::object(h) : Node();
}

void Context::add_type(const detail::TypeRecord& type) {
    if (std::find(impl_->types.begin(), impl_->types.end(), &type) == impl_->types.end())
        impl_->types.push_back(&type);
}

Expected<Node> Context::try_call(const Node& object, Symbol method, std::span<const Node> args) {
    std::vector<Node> nodes;
    nodes.reserve(args.size() + 2);
    nodes.push_back(object);
    nodes.emplace_back(method);
    nodes.insert(nodes.end(), args.begin(), args.end());
    const Ref<Array> command = Array::make_from(nodes);
    return try_eval(Node(command, Type::Command));
}

Error Context::resolve_error(const Node& object, std::string_view type) const {
    if (object.type() != Type::Object)
        return make_error(ErrorCode::Type, "expected a " + std::string(type) + " object, not " +
                                               std::string(moira::type_name(object.type())));
    if (!try_resolve(object))
        return make_error(ErrorCode::NotFound, "the " + std::string(type) + " object was removed");
    return make_error(ErrorCode::Type, "the object is a " + try_resolve(object)->type().name +
                                           ", not a " + std::string(type));
}

#if MOIRA_EXCEPTIONS
Node Context::add(ObjectBase& object, Symbol name) {
    auto r = try_add(object, name);
    if (!r) detail::raise(std::move(r).error());
    return *r;
}
#endif

// --- Raw<Var> ---

Expected<void> Raw<Var>::try_store(Node v) const {
    if (cell_) {
        *cell_ = std::move(v);
        return {};
    }
    Context::Impl& impl = ctx_->impl();
    const Node* self_node = impl.cell_if(sym_this());
    ObjectBase* o = self_node ? ctx_->try_resolve(*self_node) : nullptr;
    if (!o)
        return Unexpected(make_error(ErrorCode::NotFound,
                                     "{set [...] value} sets a property of $this, which is not set",
                                     property_->file(), property_->line()));
    const auto name = property_->size() == 1 ? (*property_)[0].try_sym() : std::nullopt;
    if (!name)
        return Unexpected(make_error(ErrorCode::Syntax, "{set [...] value} sets one property",
                                     property_->file(), property_->line()));
    return impl.set_property(*o, *name, v, *property_);
}

Node Raw<Var>::load() const {
    if (cell_) return VarRef(cell_, name_).value();
    auto r = ctx_->impl().property_path(*property_);
    return r ? std::move(*r) : Node();
}

// --- Built-ins ---

void register_object_builtins(Context& ctx) {
    // {class Name (method ($params...) body...) (method body...) ...}
    auto define_class = [](Args a) -> Expected<Node> {
        if (a.size() < 1 || a.raw(0).type() != Type::Symbol)
            return Unexpected(
                a.error("write {class Name (method ($params...) body...) ...}", ErrorCode::Syntax));
        const Symbol name = *a.raw(0).try_sym();
        Context::Impl& impl = a.context().impl();
        auto cls = std::make_unique<ScriptClass>();
        cls->name = name;
        cls->native = impl.type_named(name);
        cls->file = a.command().file();
        cls->line = a.command().line();
        for (size_t i = 1; i < a.size(); ++i) {
            const Array* h = a.raw(i).try_array();
            if (!h || h->empty() || (*h)[0].type() != Type::Symbol)
                return Unexpected(
                    a.error("a handler is (method ($params...) body...)", ErrorCode::Syntax));
            ScriptClass::Handler handler{*(*h)[0].try_sym(), {}};
            handler.body.definition = Ref<Array>(const_cast<Array*>(h));
            handler.body.body = 1;
            if (const Array* params = h->size() > 1 ? (*h)[1].try_array() : nullptr) {
                if (!parse_params(*params, handler.body))
                    return Unexpected(
                        a.error("parameters of " + quoted(handler.name) + " must be $variables",
                                ErrorCode::Syntax));
                handler.body.body = 2;
            }
            cls->handlers.push_back(std::move(handler));
        }
        if (name.index() >= impl.class_by_name.size()) impl.class_by_name.resize(name.index() + 1);
        if (ScriptClass* old = impl.class_by_name[name.index()]) {
            *old = std::move(*cls);  // a redefinition replaces the handlers (a reloaded script)
        } else {
            impl.class_by_name[name.index()] = cls.get();
            impl.classes.push_back(std::move(cls));
        }
        return Node(name);
    };
    if (auto r = ctx.try_func("class", define_class))
        r->doc(
            "`{class Name (method ($params...) body...) ...}`: script handlers. If `Name` is a "
            "bound C++ type, they apply to its objects and are tried before its C++ methods; "
            "otherwise `Name` is a script class for `new`. In a handler, `$this` is the "
            "object and `[name]` its property or parameter.");

    // {new Class [name] (param value)...}
    auto make = [](Args a) -> Expected<Node> {
        if (a.size() < 1 || a.raw(0).type() != Type::Symbol)
            return Unexpected(
                a.error("write {new Class [name] (param value)...}", ErrorCode::Syntax));
        const Symbol cls = *a.raw(0).try_sym();
        Context::Impl& impl = a.context().impl();
        ObjectBase* object = nullptr;
        ScriptClass* script = nullptr;
        if (const detail::TypeRecord* t = impl.type_named(cls)) {
            if (!t->create)
                return Unexpected(a.error(quoted(cls) + " cannot be created from script: it is not "
                                                        "default-constructible",
                                          ErrorCode::Refused));
            object = t->create();
        } else if ((script = impl.script_class(cls))) {
            object = new ScriptObject();
        } else {
            return Unexpected(a.error(quoted(cls) + " is not a class", ErrorCode::NotFound));
        }
        std::unique_ptr<ObjectBase> owner(object);
        size_t i = 1;
        Symbol name;
        if (a.size() > 1 && a.raw(1).type() == Type::Symbol) name = *a.raw(i++).try_sym();
        auto node = impl.add(*object, name, script, true);
        if (!node) return Unexpected(a.error(std::move(node).error().message, ErrorCode::Refused));
        owner.release();
        for (; i < a.size(); ++i) {
            const Array* p = a.raw(i).try_array();
            Expected<void> r;
            if (!p || p->size() != 2 || (*p)[0].type() != Type::Symbol) {
                r = Unexpected(a.error("parameters are (name value)", ErrorCode::Syntax));
            } else if (auto v = impl.eval((*p)[1]); !v) {
                r = Unexpected(std::move(v).error());
            } else {
                r = impl.set_property(*object, *(*p)[0].try_sym(), *v, a.command());
            }
            if (!r) {
                impl.remove(node->object_handle());
                delete object;
                return Unexpected(std::move(r).error());
            }
        }
        return node;
    };
    if (auto r = ctx.try_func("new", make))
        r->doc(
            "`{new Class [name] (param value)...}`: creates an object of a script class or of "
            "a default-constructible bound type, owned by the Context, and returns it.");

    auto destroy = [](Context& c, Node object) -> Expected<Node> {
        Context::Impl& impl = c.impl();
        if (auto name = object.try_sym()) object = c.object(*name);  // {delete g1}
        if (object.type() != Type::Object)
            return Unexpected(make_error(
                ErrorCode::Type,
                "delete takes an object, not a " + std::string(type_name(object.type()))));
        Context::Impl::Slot* s = impl.slot(object.object_handle());
        if (!s) return Node();
        ObjectBase* o = s->object;
        const bool owned = s->owned;
        impl.remove(object.object_handle());
        if (owned) delete o;
        return Node();
    };
    if (auto r = ctx.try_func("delete", destroy))
        r->doc(
            "`{delete object}`: removes the object (or the object of that name); one `new` "
            "created is destroyed. Its handles go stale.");
}

}  // namespace moira
