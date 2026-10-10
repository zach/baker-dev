#include <moira/symbol.hpp>

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace moira {
namespace {

// Append-only arena + open-addressed hash table of entry pointers. Never destroyed:
// symbols are immortal, and leaking the table keeps them valid during static
// destruction too.
class SymbolTable {
public:
    const detail::SymbolEntry* intern(std::string_view text) {
        const uint64_t hash = detail::hash_symbol_text(text);
        std::lock_guard lock(mutex_);
        size_t mask = slots_.size() - 1;
        for (size_t i = static_cast<size_t>(hash) & mask;; i = (i + 1) & mask) {
            const detail::SymbolEntry* e = slots_[i];
            if (!e) break;
            if (e->hash == hash && e->size == text.size() &&
                std::memcmp(e->text, text.data(), text.size()) == 0)
                return e;
        }
        if ((count_ + 1) * 2 > slots_.size()) grow();
        const detail::SymbolEntry* e = make_entry(text, hash);
        insert(e);
        ++count_;
        return e;
    }

    size_t size() {
        std::lock_guard lock(mutex_);
        return count_;
    }

private:
    static constexpr size_t kBlockSize = 64 * 1024;

    void insert(const detail::SymbolEntry* e) {
        size_t mask = slots_.size() - 1;
        size_t i = static_cast<size_t>(e->hash) & mask;
        while (slots_[i]) i = (i + 1) & mask;
        slots_[i] = e;
    }

    void grow() {
        std::vector<const detail::SymbolEntry*> old(slots_.size() * 2, nullptr);
        old.swap(slots_);
        for (const detail::SymbolEntry* e : old)
            if (e) insert(e);
    }

    const detail::SymbolEntry* make_entry(std::string_view text, uint64_t hash) {
        const size_t need = sizeof(detail::SymbolEntry) + text.size() + 1;
        char* mem = alloc(need);
        auto* chars = mem + sizeof(detail::SymbolEntry);
        if (!text.empty()) std::memcpy(chars, text.data(), text.size());
        chars[text.size()] = '\0';
        return new (mem) detail::SymbolEntry{chars, static_cast<uint32_t>(text.size()), hash};
    }

    char* alloc(size_t n) {
        n = (n + alignof(detail::SymbolEntry) - 1) & ~(alignof(detail::SymbolEntry) - 1);
        if (n > kBlockSize / 4) {
            blocks_.push_back(std::make_unique<char[]>(n));
            return blocks_.back().get();
        }
        if (!cur_ || used_ + n > kBlockSize) {
            blocks_.push_back(std::make_unique<char[]>(kBlockSize));
            cur_ = blocks_.back().get();
            used_ = 0;
        }
        char* p = cur_ + used_;
        used_ += n;
        return p;
    }

    std::mutex mutex_;
    std::vector<const detail::SymbolEntry*> slots_ = std::vector<const detail::SymbolEntry*>(1024);
    size_t count_ = 0;
    std::vector<std::unique_ptr<char[]>> blocks_;
    char* cur_ = nullptr;
    size_t used_ = 0;
};

SymbolTable& table() {
    static SymbolTable* t = new SymbolTable;  // intentionally leaked; see above
    return *t;
}

}  // namespace

Symbol::Symbol(std::string_view text)
    : e_(text.empty() ? &detail::kEmptySymbolEntry : table().intern(text)) {}

size_t Symbol::table_size() noexcept {
    return table().size();
}

}  // namespace moira
