#pragma once

// The inside of a Context, shared by the files of moira::script.

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

struct Global {
    Symbol name;
    std::string doc;
};

// "(int float)", as the reference and messages write a signature.
[[nodiscard]] std::string signature_text(const detail::Overload& o);
// "2 arguments", "1 to 2 arguments", "at least 1 argument".
[[nodiscard]] std::string arity_text(uint32_t min, uint32_t max);
// Registers the functions every Context starts with (builtins.cpp).
void register_builtins(Context& ctx);

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
    [[nodiscard]] Expected<Node> call_overloaded(const Function& f, const Array& command);
    [[nodiscard]] Expected<Node> run(std::span<const Node> nodes);

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
