#pragma once

// The inside of a Context, shared by the files of moira::script.

#include <moira/object.hpp>
#include <moira/script.hpp>

#include <memory>
#include <string>
#include <vector>

namespace moira {

struct Function : FuncRecord {
    Tier tier = Tier::Native;
    Status status = Status::Normal;
    Symbol replacement;  // for Deprecated
    std::string doc;
    std::vector<detail::Overload> overloads;
    Symbol file;  // where a script function is defined
    int32_t line = 0;
};

// A {func} body: run with the parameters bound.
struct ScriptBody {
    std::vector<Symbol> params;
    Ref<Array> definition;
    size_t body = 0;  // index of the first body node in `definition`
};

// A {class}: script handlers for a native type (`native` set) or a script-only class.
struct ScriptClass {
    struct Handler {
        Symbol name;
        ScriptBody body;
    };
    Symbol name;
    const detail::TypeRecord* native = nullptr;
    std::vector<Handler> handlers;
    Symbol file;
    int32_t line = 0;

    [[nodiscard]] const Handler* handler(Symbol method) const noexcept {
        for (const Handler& h : handlers)
            if (h.name == method) return &h;
        return nullptr;
    }
};

struct Global {
    Symbol name;
    std::string doc;
};

// "(int float)", as the reference and messages write a signature.
[[nodiscard]] std::string signature_text(const detail::Overload& o);
// "2 arguments", "1 to 2 arguments", "at least 1 argument".
[[nodiscard]] std::string arity_text(uint32_t min, uint32_t max);
// Registers the functions every Context starts with (builtins.cpp, object.cpp).
void register_builtins(Context& ctx);
void register_object_builtins(Context& ctx);
// Parses a {func}-style parameter list and body from a definition array, starting at
// `params` (the index of the ($a ...) list).
[[nodiscard]] bool parse_params(const Array& params, ScriptBody& body);

struct Context::Impl {
    static constexpr uint32_t kPageBits = 10;
    static constexpr uint32_t kMaxDepth = 1000;

    explicit Impl(Context& c) : self(c) {}

    [[nodiscard]] Function* find(Symbol name) const noexcept {
        const uint32_t i = name.index();
        return i < by_index.size() ? by_index[i] : nullptr;
    }
    [[nodiscard]] const Node* cell_if(Symbol name) const noexcept {
        const uint32_t i = name.index();
        const uint32_t page = i >> kPageBits;
        return page < pages.size() && pages[page] ? &pages[page][i & ((1u << kPageBits) - 1)]
                                                  : nullptr;
    }
    [[nodiscard]] Node* cell(Symbol name);
    [[nodiscard]] Function& function(Symbol name);

    [[nodiscard]] Expected<Node> eval(const Node& n);
    [[nodiscard]] Expected<Node> call(const Array& command);
    // Calls the overload set `overloads` of `name` with the arguments of `command` from
    // index `first` (2 for a method, whose object is `self`).
    [[nodiscard]] Expected<Node> invoke_set(Symbol name,
                                            const std::vector<detail::Overload>& overloads,
                                            const Array& command, size_t first, ObjectBase* self);
    [[nodiscard]] Expected<Node> run(std::span<const Node> nodes);
    // Runs a script function or handler: arguments evaluated, then bound with $this.
    [[nodiscard]] Expected<Node> run_script(const ScriptBody& body, Args& args,
                                            const Node* this_node);

    // --- Objects (object.cpp). ---
    struct Slot {
        ObjectBase* object = nullptr;
        uint32_t salt = 1;
        Symbol name;
        ScriptClass* script = nullptr;  // the class of a script-only object
        bool owned = false;             // created by {new}: the Context deletes it
    };
    [[nodiscard]] Slot* slot(ObjectHandle h) noexcept {
        const uint32_t i = h.index();
        return i < slots.size() && slots[i].object && slots[i].salt == h.salt() ? &slots[i]
                                                                                : nullptr;
    }
    [[nodiscard]] ObjectHandle named(Symbol name) const noexcept {
        const uint32_t i = name.index();
        return i < by_name.size() ? by_name[i] : ObjectHandle{};
    }
    [[nodiscard]] Expected<Node> add(ObjectBase& object, Symbol name, ScriptClass* script,
                                     bool owned);
    void remove(ObjectHandle h) noexcept;
    [[nodiscard]] Expected<Node> send(ObjectHandle target, const Array& command);
    [[nodiscard]] Expected<Node> property_path(const Array& path);
    [[nodiscard]] Expected<void> set_property(ObjectBase& object, Symbol name, const Node& value,
                                              const Array& at);
    [[nodiscard]] ScriptClass* script_class(Symbol name) const noexcept;
    [[nodiscard]] const detail::TypeRecord* type_named(Symbol name) const noexcept;
    // The name a type goes by in messages: a script class's, else the native type's.
    [[nodiscard]] std::string class_name(const Slot& s) const;

    // Binds `name` to `value` until the innermost Rebind ends.
    void bind(Symbol name, Node value) {
        Node* c = cell(name);
        saved.emplace_back(c, std::move(*c));
        *c = std::move(value);
    }

    Context& self;
    std::vector<Function*> by_index;  // by Symbol::index()
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<Node[]>> pages;  // variable cells, by Symbol::index()
    std::vector<std::pair<Node*, Node>> saved;   // values shadowed by bind()
    std::vector<Node> kept;                      // evaluated values a call refers to
    std::vector<Global> globals;
    std::vector<Slot> slots;
    std::vector<uint32_t> free_slots;
    std::vector<ObjectHandle> by_name;  // by Symbol::index()
    std::vector<const detail::TypeRecord*> types;
    std::vector<std::unique_ptr<ScriptClass>> classes;
    std::vector<ScriptClass*> class_by_name;  // by Symbol::index()
    uint32_t depth = 0;
    uintptr_t stack_base = 0;             // the outermost call's stack address
    uintptr_t stack_budget = 256 * 1024;  // see Context::set_stack_budget
    NoticeSink sink = nullptr;
    void* sink_user = nullptr;
};

// Restores every variable bound since it was made: dynamic scope, undone even on error.
class Rebind {
public:
    explicit Rebind(Context::Impl& impl) noexcept : impl_(impl), mark_(impl.saved.size()) {}
    ~Rebind() {
        while (impl_.saved.size() > mark_) {
            auto& [cell, old] = impl_.saved.back();
            *cell = std::move(old);
            impl_.saved.pop_back();
        }
    }
    Rebind(const Rebind&) = delete;
    Rebind& operator=(const Rebind&) = delete;

private:
    Context::Impl& impl_;
    size_t mark_;
};

}  // namespace moira
