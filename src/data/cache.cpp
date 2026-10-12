// The binary cache (spec §9, D70). The writer lays a tree out as the loader will use it:
// the file, after a block header, is the tree's arena block, so loading is one read into
// one allocation, then one pass that turns offsets into pointers.

#include <moira/cache.hpp>

#include "arena.hpp"
#include "common/containers.hpp"
#include "text.hpp"

#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace moira {
namespace detail {

// What the format relies on in the layouts of Node and Array.
struct CacheLayout {
    static constexpr size_t kValue = offsetof(Node, value_);
    static constexpr size_t kType = offsetof(Node, type_);
    static constexpr size_t kSize = offsetof(Array, size_);
    static constexpr size_t kFile = offsetof(Array, file_);
    static const Node* slots(const Array& a) noexcept { return a.data(); }
};

namespace {

using L = CacheLayout;
static_assert(L::kValue == 0 && L::kType == 8 && L::kSize == 4 && L::kFile == 8);
static_assert(sizeof(String) == 8 && sizeof(Symbol) == 8);

constexpr char kMagic[10] = {'M', 'O', 'I', 'R', 'A', '.', 'D', 'A', 'T', 'B'};
constexpr uint16_t kVersion = 1;
constexpr uint32_t kEndian = 0x01020304;

// Offsets count from the start of the file. The blob follows the header: arrays from the
// root on, breadth first so that siblings lie together, then strings. The symbol section
// is a zeroed 8-byte slot per symbol, which the loader fills with the interned Symbol,
// then each name as a 32-bit length and its bytes.
struct Header {
    char magic[10];
    uint16_t version;
    uint32_t endian;
    uint8_t pointer_bytes;
    uint8_t flags;
    uint16_t reserved;
    uint64_t hash;  // of every byte after the header
    uint32_t file_bytes;
    uint32_t root;
    uint32_t arrays_end;  // strings start here
    uint32_t blob_end;
    uint32_t symbols;
    uint32_t symbol_count;
    uint32_t deps;  // M8b: the files a load read, and its defines
    uint32_t dep_count;
    uint32_t defines;
    uint32_t define_count;
};
static_assert(sizeof(Header) == 72);

constexpr uint32_t round8(size_t n) noexcept {
    return static_cast<uint32_t>((n + 7) & ~size_t{7});
}
uint32_t array_bytes(uint32_t size) noexcept {
    return static_cast<uint32_t>(sizeof(Array) + (size ? size : 1) * sizeof(Node));
}

// A checksum, not a cipher: four lanes of multiply and rotate, near memory speed.
uint64_t hash_bytes(const char* p, size_t n) noexcept {
    constexpr uint64_t k = 0x9E3779B97F4A7C15ull;
    uint64_t h[4] = {k, k + 1, k + 2, k + 3};
    size_t i = 0;
    for (; i + 32 <= n; i += 32)
        for (int l = 0; l < 4; ++l) {
            uint64_t w;
            std::memcpy(&w, p + i + 8 * l, 8);
            h[l] = std::rotl((h[l] ^ w) * k, 29);
        }
    uint64_t r = n;
    for (; i < n; ++i) r = (r ^ static_cast<unsigned char>(p[i])) * k;
    for (const uint64_t x : h) r = std::rotl((r ^ x) * k, 31);
    return r ^ (r >> 32);
}

template <class T>
void put(Str& out, size_t at, const T& v) noexcept {
    std::memcpy(out.data() + at, &v, sizeof v);
}

class Writer {
public:
    Expected<void> run(const Array& root, Str& out) {
        // Lay out arrays breadth first, sharing what the tree shares; then strings.
        uint32_t end = sizeof(Header);
        if (!place(&root, end)) return limit();
        for (size_t i = 0; i < arrays_.size(); ++i) {
            const Array& a = *arrays_[i];
            for (const Node& n : a) {
                if (n.type() == Type::Func || n.type() == Type::Object)
                    return Unexpected(make_error(ErrorCode::Refused,
                                                 "a cache holds data, and " + preview(n) +
                                                     " is a " + Str(type_name(n.type())),
                                                 a.file(), a.line()));
                if (const Array* c = n.array(); c && !at_.count(c) && !place(c, end))
                    return limit();
                if (const String* s = n.string(); s && !at_.count(s)) {
                    at_[s] = 0;
                    strings_.push_back(s);
                }
            }
        }
        const uint32_t arrays_end = end;
        for (const String* s : strings_) {
            at_[s] = end;
            if (end + size_t{round8(sizeof(String) + s->size() + 1)} > UINT32_MAX) return limit();
            end += round8(sizeof(String) + s->size() + 1);
        }

        out.clear();
        out.resize(end);
        for (const Array* a : arrays_) write_array(*a, out);
        for (const String* s : strings_) {
            const uint32_t at = at_[s];
            put(out, at, kBlock + at);
            put(out, at + 4, s->size());
            std::memcpy(out.data() + at + sizeof(String), s->data(), s->size());
        }
        // The symbols.
        const uint32_t symbols = end;
        out.resize(symbols + symbols_.size() * 8);
        for (const Symbol s : symbols_) {
            const auto size = static_cast<uint32_t>(s.str().size());
            out.append(std::string_view(reinterpret_cast<const char*>(&size), 4));
            out.append(s.str());
        }
        out.resize(round8(out.size()));
        if (out.size() > UINT32_MAX) return limit();

        Header h{};
        std::memcpy(h.magic, kMagic, sizeof kMagic);
        h.version = kVersion;
        h.endian = kEndian;
        h.pointer_bytes = sizeof(void*);
        h.file_bytes = static_cast<uint32_t>(out.size());
        h.root = sizeof(Header);
        h.arrays_end = arrays_end;
        h.blob_end = symbols;
        h.symbols = symbols;
        h.symbol_count = static_cast<uint32_t>(symbols_.size());
        h.deps = h.defines = h.file_bytes;
        h.hash = hash_bytes(out.data() + sizeof(Header), out.size() - sizeof(Header));
        put(out, 0, h);
        return {};
    }

private:
    static constexpr uint32_t kBlock = Arena::kBlockHeader;

    static Unexpected limit() {
        return Unexpected(make_error(ErrorCode::Limit, "a cache holds at most 4 GB"));
    }
    bool place(const Array* a, uint32_t& end) {
        if (end + size_t{array_bytes(a->size())} > UINT32_MAX) return false;
        at_[a] = end;
        end += array_bytes(a->size());
        arrays_.push_back(a);
        return true;
    }
    uint64_t symbol(Symbol s) {
        auto [it, added] = symbol_index_.emplace(s.id(), static_cast<uint32_t>(symbols_.size()));
        if (added) symbols_.push_back(s);
        return it->second;
    }
    void write_array(const Array& a, Str& out) {
        const uint32_t at = at_[&a];
        std::memcpy(out.data() + at, &a, sizeof(Array));
        put(out, at, kBlock + at);
        put(out, at + L::kFile, symbol(a.file()));
        const Node* nodes = L::slots(a);
        for (uint32_t i = 0; i < (a.size() ? a.size() : 1); ++i) {
            const Node& n = nodes[i];
            const size_t p = at + sizeof(Array) + i * sizeof(Node);
            std::memcpy(out.data() + p, &n, L::kType + 1);  // the payload and type; aux stays 0
            switch (n.type()) {
                case Type::Symbol: put(out, p, symbol(*n.try_sym())); break;
                case Type::Var: put(out, p, symbol(n.var_name())); break;
                case Type::String: put(out, p, uint64_t{at_[n.string()]}); break;
                case Type::Array:
                case Type::Command:
                case Type::Property: put(out, p, uint64_t{at_[n.array()]}); break;
                default: break;
            }
        }
    }

    Vec<const Array*> arrays_;
    Vec<const String*> strings_;
    HashMap<const void*, uint32_t> at_;  // where each array and string goes
    Vec<Symbol> symbols_;
    HashMap<const void*, uint32_t> symbol_index_;
};

Unexpected bad(std::string_view name, std::string_view why, ErrorCode code = ErrorCode::Io) {
    return Unexpected(make_error(code, "'" + Str(name) + "' " + Str(why)));
}

// Checks the cache at `file` and fixes it up in place, in the arena that holds it.
Expected<Ref<Array>> load(char* file, size_t bytes, std::string_view name) {
    Header h;
    if (bytes < sizeof h) return bad(name, "is not a Moira cache");
    std::memcpy(&h, file, sizeof h);
    if (std::memcmp(h.magic, kMagic, sizeof kMagic) != 0) return bad(name, "is not a Moira cache");
    if (h.version != kVersion)
        return bad(name,
                   "is a cache of format version " + decimal(h.version) +
                       "; this build reads version " + decimal(kVersion),
                   ErrorCode::Refused);
    if (h.endian != kEndian || h.pointer_bytes != sizeof(void*))
        return bad(name, "was written on a platform of another endianness or pointer size",
                   ErrorCode::Refused);
    if (h.flags != 0) return bad(name, "is enciphered", ErrorCode::Refused);
    const bool fits = h.file_bytes == bytes && h.root >= sizeof h && h.root < h.arrays_end &&
                      h.arrays_end <= h.blob_end && h.blob_end <= h.symbols && h.symbols % 8 == 0 &&
                      h.symbols <= bytes && h.symbol_count <= (bytes - h.symbols) / 8;
    if (!fits || h.hash != hash_bytes(file + sizeof h, bytes - sizeof h))
        return bad(name, "is damaged");

    // Intern the symbols into their slots.
    char* slots = file + h.symbols;
    size_t p = h.symbols + size_t{h.symbol_count} * 8;
    for (uint32_t i = 0; i < h.symbol_count; ++i) {
        uint32_t size;
        if (bytes - p < 4) return bad(name, "is damaged");
        std::memcpy(&size, file + p, 4);
        p += 4;
        if (bytes - p < size) return bad(name, "is damaged");
        const void* id = Symbol(std::string_view(file + p, size)).id();
        std::memcpy(slots + size_t{i} * 8, &id, 8);
        p += size;
    }

    // One pass over the arrays: offsets become pointers, and indexes Symbols.
    auto symbol = [&](char* at) {
        uint64_t i;
        std::memcpy(&i, at, 8);
        if (i >= h.symbol_count) return false;
        std::memcpy(at, slots + i * 8, 8);
        return true;
    };
    auto pointer = [&](char* at, uint32_t from, uint32_t to) {
        uint64_t off;
        std::memcpy(&off, at, 8);
        if (off < from || off >= to || off % 8 != 0) return false;
        void* ptr = file + off;
        std::memcpy(at, &ptr, 8);
        return true;
    };
    for (size_t a = sizeof h; a < h.arrays_end;) {
        uint32_t size;
        std::memcpy(&size, file + a + L::kSize, 4);
        const size_t end = a + array_bytes(size);
        if (h.arrays_end - a < sizeof(Array) || end > h.arrays_end || !symbol(file + a + L::kFile))
            return bad(name, "is damaged");
        for (char* n = file + a + sizeof(Array); n < file + end; n += sizeof(Node)) {
            bool ok = true;
            switch (static_cast<Type>(n[L::kType])) {
                case Type::Int:
                case Type::Float: break;
                case Type::Symbol:
                case Type::Var: ok = symbol(n); break;
                case Type::String: ok = pointer(n, h.arrays_end, h.blob_end); break;
                case Type::Array:
                case Type::Command:
                case Type::Property: ok = pointer(n, sizeof h, h.arrays_end); break;
                default: ok = false;
            }
            if (!ok) return bad(name, "is damaged");
        }
        a = end;
    }
    return Ref<Array>(reinterpret_cast<Array*>(file + h.root));
}

const Allocator& allocator_of(const Allocator* a) noexcept {
    return a ? *a : default_allocator();
}

}  // namespace
}  // namespace detail

Expected<void> write_cache(const Array& root, TextSink out) {
    Str bytes;
    if (auto r = detail::Writer().run(root, bytes); !r) return r;
    out(bytes);
    return {};
}

Expected<void> write_cache(const Array& root, std::string_view path) {
    Str bytes;
    if (auto r = detail::Writer().run(root, bytes); !r) return r;
    const Str file = detail::normalize_path(path);
    std::FILE* f = std::fopen(file.c_str(), "wb");
    bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f) ok = std::fclose(f) == 0 && ok;
    if (!ok) return Unexpected(make_error(ErrorCode::Io, "cannot write '" + file + "'"));
    return {};
}

Expected<Ref<Array>> read_cache_bytes(std::string_view bytes, std::string_view name,
                                      const Allocator* allocator) {
    char* file;
    const detail::Arena::Handle arena(
        detail::Arena::create_holding(detail::allocator_of(allocator), bytes.size(), file));
    if (!bytes.empty()) std::memcpy(file, bytes.data(), bytes.size());
    return detail::load(file, bytes.size(), name);
}

Expected<Ref<Array>> read_cache(std::string_view path, LoadSession& session) {
    auto cannot = [&] {
        return Unexpected(make_error(ErrorCode::Io, "cannot read '" + Str(path) + "'"));
    };
    if (!detail::reads_disk(session)) {
        const Str file = detail::normalize_path(path);
        Str bytes;
        if (!session.reader()(file, detail::str_out(bytes))) return cannot();
        return read_cache_bytes(bytes, file, session.allocator);
    }
    // fopen needs a terminated path: on the stack, so the tree is the only allocation.
    char name[512];
    Str long_name;
    const char* c_path = name;
    if (path.size() < sizeof name) {
        std::memcpy(name, path.data(), path.size());
        name[path.size()] = '\0';
    } else {
        long_name = path;
        c_path = long_name.c_str();
    }
    std::FILE* f = std::fopen(c_path, "rb");
    if (!f) return cannot();
    std::setvbuf(f, nullptr, _IONBF, 0);  // read straight into the arena
    long size = -1;
    if (std::fseek(f, 0, SEEK_END) == 0) size = std::ftell(f);
    if (size < 0 || static_cast<unsigned long>(size) > UINT32_MAX ||
        std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return cannot();
    }
    const auto bytes = static_cast<size_t>(size);
    char* data;
    const detail::Arena::Handle arena(
        detail::Arena::create_holding(detail::allocator_of(session.allocator), bytes, data));
    const bool ok = std::fread(data, 1, bytes, f) == bytes;
    std::fclose(f);
    if (!ok) return cannot();
    return detail::load(data, bytes, path);
}

Expected<Ref<Array>> read_cache(std::string_view path) {
    LoadSession session;
    return read_cache(path, session);
}

}  // namespace moira
