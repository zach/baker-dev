// The binary cache (spec §9, D70). The writer lays a tree out as the loader will use it:
// the file, after a block header, is the tree's arena block, so loading is one read into
// one allocation, then one pass that turns offsets into pointers.

#include <moira/cache.hpp>

#include "arena.hpp"
#include "common/containers.hpp"
#include "session.hpp"
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
//
// A cache written from a recorded load has a second array, `meta`, laid out with the tree
// (D72): the files the load read, the defines it began with, and its effects on the session.
//   (files ("path" size hash) ...)     size -1: looked for and absent
//   (before NAME hash ...)             0 for a flag, else the hash of the macro's text
//   (define NAME body-or-0 ...) (undefine NAME ...) (autorun {command} ...)
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
    uint32_t meta;  // 0: none
    uint32_t reserved2;
};
static_assert(sizeof(Header) == 64);
constexpr uint8_t kEnciphered = 1;

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
    Expected<void> run(const Array& root, const Array* meta, const Cipher* cipher, Str& out) {
        // Lay out arrays breadth first, sharing what the tree shares; then strings.
        uint32_t end = sizeof(Header);
        if (!place(&root, end) || (meta && !place(meta, end))) return limit();
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
        h.meta = meta ? at_[meta] : 0;
        if (cipher) {
            h.flags = kEnciphered;
            cipher->encrypt(cipher->user,
                            {reinterpret_cast<std::byte*>(out.data()) + sizeof(Header),
                             out.size() - sizeof(Header)});
        }
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

// Checks the cache at `file` and fixes it up in place, in the arena that holds it. `meta` is
// set to the cache's record of its load, or null.
Expected<Ref<Array>> load(char* file, size_t bytes, std::string_view name, const Cipher* cipher,
                          const Array*& meta) {
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
    if ((h.flags & kEnciphered) && !cipher)
        return bad(name, "is enciphered, and the session has no Cipher", ErrorCode::Refused);
    auto array_at = [&](uint32_t off) {
        return off >= sizeof h && off < h.arrays_end && off % 8 == 0;
    };
    const bool fits = h.file_bytes == bytes && array_at(h.root) &&
                      (h.meta == 0 || array_at(h.meta)) && (h.flags & ~kEnciphered) == 0 &&
                      h.arrays_end <= h.blob_end && h.blob_end <= h.symbols && h.symbols % 8 == 0 &&
                      h.symbols <= bytes && h.symbol_count <= (bytes - h.symbols) / 8;
    if (!fits || h.hash != hash_bytes(file + sizeof h, bytes - sizeof h))
        return bad(name, "is damaged");
    if (h.flags & kEnciphered)
        cipher->decrypt(cipher->user,
                        {reinterpret_cast<std::byte*>(file) + sizeof h, bytes - sizeof h});

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
    meta = h.meta ? reinterpret_cast<const Array*>(file + h.meta) : nullptr;
    return Ref<Array>(reinterpret_cast<Array*>(file + h.root));
}

// --- The record of a load (D72). ---

uint64_t body_hash(const Ref<Array>& body) {
    return body ? content_hash(text_of(Node(body))) : 0;
}

Node list(Vec<Node>& nodes) {
    return Node(Array::make_from({nodes.data(), nodes.size()}));
}

// The meta array for a load the session recorded, or null.
Ref<Array> record_of(const LoadSession& s) {
    using A = SessionAccess;
    if (!A::recorded(s)) return nullptr;
    Vec<Node> files{Node(Symbol("files"))}, before{Node(Symbol("before"))},
        define{Node(Symbol("define"))}, undefine{Node(Symbol("undefine"))},
        autorun{Node(Symbol("autorun"))};
    for (const CacheFile& f : A::files(s)) {
        Vec<Node> entry{Node(String::make(f.path)), Node(f.size), Node(f.hash)};
        files.push_back(list(entry));
    }
    const A::Defines &was = A::defines_before(s), &now = A::defines(s);
    for (const auto& [name, body] : was) {
        before.push_back(Node(name));
        before.push_back(Node(body_hash(body)));
        if (!now.count(name)) undefine.push_back(Node(name));
    }
    for (const auto& [name, body] : now) {
        const auto it = was.find(name);
        if (it != was.end() && it->second == body) continue;
        define.push_back(Node(name));
        define.push_back(body ? Node(body) : Node(0));
    }
    for (const Node& n : A::new_autorun(s)) autorun.push_back(n);
    Vec<Node> meta{list(files), list(before), list(define), list(undefine), list(autorun)};
    return Array::make_from({meta.data(), meta.size()});
}

// Whether the files the load read are unchanged, and the session's defines are those it
// began with.
bool fresh(const Array& meta, LoadSession& s) {
    const Array* files = meta.try_find("files");
    const Array* before = meta.try_find("before");
    if (!files || !before) return false;
    Str text;
    for (const Node& n : files->values()) {
        const Array* f = n.array();
        if (!f || f->size() != 3 || !f->nodes()[0].string()) return false;
        text.clear();
        const bool read = s.reader()(f->nodes()[0].string()->view(), str_out(text));
        const int64_t size = f->nodes()[1].try_int().value_or(-1);
        const auto hash = static_cast<uint64_t>(f->nodes()[2].try_int().value_or(0));
        if (size < 0
                ? read
                : !read || static_cast<int64_t>(text.size()) != size || content_hash(text) != hash)
            return false;
    }
    const SessionAccess::Defines& now = SessionAccess::defines(s);
    if (before->size() % 2 == 0 || (before->size() - 1) / 2 != now.size()) return false;
    for (uint32_t i = 1; i + 1 < before->size(); i += 2) {
        const auto it = now.find((*before)[i].try_sym().value_or(Symbol()));
        if (it == now.end() ||
            body_hash(it->second) != static_cast<uint64_t>((*before)[i + 1].try_int().value_or(0)))
            return false;
    }
    return true;
}

// Leaves the session as the recorded load left it.
void replay(const Array& meta, LoadSession& s) {
    if (const Array* d = meta.try_find("define"))
        for (uint32_t i = 1; i + 1 < d->size(); i += 2) {
            const Symbol name = (*d)[i].try_sym().value_or(Symbol());
            if (const Array* body = (*d)[i + 1].array())
                s.define(name, Ref<Array>(const_cast<Array*>(body)));
            else
                s.define(name);
        }
    if (const Array* u = meta.try_find("undefine"))
        for (const Node& n : u->values())
            if (const auto name = n.try_sym()) s.undefine(*name);
    if (const Array* a = meta.try_find("autorun"))
        for (const Node& n : a->values()) SessionAccess::add_autorun(s, n);
}

const Allocator& allocator_of(const LoadSession& s) noexcept {
    return s.allocator ? *s.allocator : default_allocator();
}

Expected<void> bytes_of(const Array& root, const LoadSession* session, Str& out) {
    Ref<Array> meta = session ? record_of(*session) : nullptr;
    return Writer().run(root, meta.get(), session ? session->cipher : nullptr, out);
}

Expected<Ref<Array>> from_bytes(std::string_view bytes, std::string_view name,
                                const LoadSession& session, const Array*& meta) {
    char* file;
    const Arena::Handle arena(Arena::create_holding(allocator_of(session), bytes.size(), file));
    if (!bytes.empty()) std::memcpy(file, bytes.data(), bytes.size());
    return load(file, bytes.size(), name, session.cipher, meta);
}

// Loads the cache at `path` without replaying it.
Expected<Ref<Array>> open(std::string_view path, const LoadSession& session, const Array*& meta) {
    auto cannot = [&] {
        return Unexpected(make_error(ErrorCode::Io, "cannot read '" + Str(path) + "'"));
    };
    if (!SessionAccess::reads_disk(session)) {
        const Str file = normalize_path(path);
        Str bytes;
        if (!session.reader()(file, str_out(bytes))) return cannot();
        return from_bytes(bytes, file, session, meta);
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
    const Arena::Handle arena(Arena::create_holding(allocator_of(session), bytes, data));
    const bool ok = std::fread(data, 1, bytes, f) == bytes;
    std::fclose(f);
    if (!ok) return cannot();
    return load(data, bytes, path, session.cipher, meta);
}

Expected<Ref<Array>> replayed(Expected<Ref<Array>> r, const Array* meta, LoadSession& session) {
    if (r && meta) replay(*meta, session);
    return r;
}
template <class Load>
Expected<Ref<Array>> load_and_replay(LoadSession& session, Load load) {
    const Array* meta = nullptr;
    auto r = load(meta);  // sets meta, so it is called before replayed() reads it
    return replayed(std::move(r), meta, session);
}

}  // namespace
}  // namespace detail

uint64_t detail::content_hash(std::string_view bytes) noexcept {
    return hash_bytes(bytes.data(), bytes.size());
}

Ref<Array> detail::read_fresh_cache(std::string_view path, LoadSession& session) {
    const Array* meta = nullptr;
    auto r = open(path, session, meta);
    if (!r || !meta || !fresh(*meta, session)) return nullptr;
    replay(*meta, session);
    return std::move(*r);
}

Expected<void> write_cache(const Array& root, TextSink out, const LoadSession* session) {
    Str bytes;
    if (auto r = detail::bytes_of(root, session, bytes); !r) return r;
    out(bytes);
    return {};
}

Expected<void> write_cache(const Array& root, std::string_view path, const LoadSession* session) {
    Str bytes;
    if (auto r = detail::bytes_of(root, session, bytes); !r) return r;
    const Str file = detail::normalize_path(path);
    std::FILE* f = std::fopen(file.c_str(), "wb");
    bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f) ok = std::fclose(f) == 0 && ok;
    if (!ok) return Unexpected(make_error(ErrorCode::Io, "cannot write '" + file + "'"));
    return {};
}

Expected<Ref<Array>> read_cache_bytes(std::string_view bytes, std::string_view name,
                                      LoadSession& session) {
    return detail::load_and_replay(session, [&](const Array*& meta) {
        return detail::from_bytes(bytes, name, session, meta);
    });
}

Expected<Ref<Array>> read_cache_bytes(std::string_view bytes, std::string_view name) {
    LoadSession session;
    return read_cache_bytes(bytes, name, session);
}

Expected<Ref<Array>> read_cache(std::string_view path, LoadSession& session) {
    return detail::load_and_replay(
        session, [&](const Array*& meta) { return detail::open(path, session, meta); });
}

Expected<Ref<Array>> read_cache(std::string_view path) {
    LoadSession session;
    return read_cache(path, session);
}

bool cache_fresh(std::string_view cache_path, LoadSession& session) {
    const Array* meta = nullptr;
    auto r = detail::open(cache_path, session, meta);
    return r && meta && detail::fresh(*meta, session);
}

Expected<Ref<Array>> read_cached(std::string_view text_path, std::string_view cache_path,
                                 LoadSession& session) {
    if constexpr (kShipping) {  // shipping trusts the cache, and writes none
        if (auto r = read_cache(cache_path, session)) return r;
        return read_file(text_path, session);
    } else {
        if (Ref<Array> fresh = detail::read_fresh_cache(cache_path, session)) return fresh;
        const bool was = std::exchange(session.record_for_cache, true);
        auto root = read_file(text_path, session);
        session.record_for_cache = was;
        // A cache that cannot be written costs the next launch a parse, not this one its
        // tree.
        if (root) (void)write_cache(**root, cache_path, &session);
        return root;
    }
}

}  // namespace moira
