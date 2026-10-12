#pragma once

// Reading DTA text into trees.
//
//   moira::Ref<moira::Array> menu = moira::read_file("menu.dta");
//
// The parser is also the preprocessor. Directives are handled while reading and never
// appear in the tree:
//
//   #include path        splice another file's top-level nodes here
//   #include_opt path    the same, but a missing file is not an error
//   #require path        the same, once per session: later #requires of that file, from
//                        any file the session reads, are no-ops (for packs, spec §1)
//   #merge path          merge another file into the enclosing array, keeping existing
//                        keys and adding missing ones (docs/decisions.md D1)
//   #ifdef N / #ifndef N / #else / #endif
//   #define N            define N (for #ifdef)
//   #define N (body)     define N as a macro whose body is the array's contents (the
//                        array must start on the same line)
//   #undef N
//   #autorun {command}   collect the command into LoadSession::autorun
//   [N node ...]         at the top level of a file: define macro N
//
// Packs (spec §1, D80). A file whose first form is
//   (pack name (level L) (requires other ...))
// is a pack at level L of the session's pack levels (set_pack_levels, hardest first); the
// form is recorded and left out of the tree. Expanding a macro that a softer pack defined,
// in a harder pack's file, is a warning (the one-way rule): it is kept in warnings(), with
// file and line. Files that declare no pack are unconstrained.
//
// A macro name written as a bare symbol is replaced by the macro's nodes. Expansion
// shares arrays instead of copying them. Paths may be "quoted" or bare.
//
// Nothing in a file can crash the reader: malformed input yields a located Error, and
// ParseLimits bounds nesting, includes, and total size.

#include <moira/array.hpp>
#include <moira/error.hpp>
#include <moira/source_map.hpp>

#include <moira/callback.hpp>
#include <moira/list.hpp>
#include <moira/text.hpp>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace moira {

struct Cipher;  // cache.hpp
namespace detail {
class Parser;
struct SessionAccess;  // the binary cache's view of a session
}  // namespace detail

struct ParseLimits {
    uint32_t max_depth = 256;                // array nesting, including spliced macro arrays
    uint32_t max_include_depth = 32;         // #include / #merge nesting
    uint32_t max_includes = 4096;            // files opened per top-level read
    uint64_t max_nodes = uint64_t{1} << 26;  // nodes created per top-level read
};

// A file that declared itself a pack (above).
struct Pack {
    Symbol name;
    Symbol level;
    uint32_t rank = 0;  // the level's place in the session's pack levels; 0 is the hardest
    Symbol file;
    List<Symbol> requires_packs;
};

// State shared by all files read through it. Macros and defines persist from one file to
// the next, as in the original; a fresh session starts empty. Copying a session copies
// its defines, macros, include directories, reader, and limits.
class LoadSession {
public:
    // Writes the whole file at `path` to `out` and returns true, or returns false if it
    // cannot read it.
    using Reader = Callback<bool(std::string_view path, TextSink out)>;

    LoadSession();
    LoadSession(const LoadSession& other);
    LoadSession(LoadSession&& other) noexcept;
    LoadSession& operator=(const LoadSession& other);
    LoadSession& operator=(LoadSession&& other) noexcept;
    ~LoadSession();

    void define(Symbol name);                   // a flag, for #ifdef
    void define(Symbol name, Ref<Array> body);  // a macro; null body means a flag
    void undefine(Symbol name);
    [[nodiscard]] bool is_defined(Symbol name) const;
    [[nodiscard]] const Array* macro(Symbol name) const;  // null unless a macro

    // Replaces file access (default: the local filesystem), e.g. to serve files from
    // memory or a socket. A null reader restores the default.
    void set_reader(Reader reader);
    [[nodiscard]] const Reader& reader() const noexcept;

    // Searched, in order, after the including file's directory.
    void add_include_dir(std::string_view dir);
    void clear_include_dirs();
    // #autorun commands, in load order.
    [[nodiscard]] std::span<const Node> autorun() const noexcept;
    void clear_autorun();

    // The pack levels, hardest first: {"engine", "game", "task"}.
    void set_pack_levels(std::initializer_list<Symbol> levels);
    void set_pack_levels(std::span<const Symbol> levels);
    [[nodiscard]] std::span<const Pack> packs() const noexcept;
    [[nodiscard]] const Pack* pack_of(Symbol file) const noexcept;  // null: not a pack
    // Warnings from the files read, in order (the one-way rule), until cleared.
    [[nodiscard]] std::span<const Error> warnings() const noexcept;
    void clear_warnings();

    ParseLimits limits;
    // Set for an edit-mode parse: records source text and node spans (source_map.hpp).
    // Null (the default) is a gameplay parse, which keeps only the tree.
    SourceMap* source_map = nullptr;
    // Set before a load that will be cached (write_cache): each top-level read then records
    // the files it read and the defines it began with, so the cache can tell when it is
    // stale and replay the load's effects on a session (docs/decisions.md D72).
    bool record_for_cache = false;
    // Enciphers the caches written with this session and deciphers those read (cache.hpp).
    const Cipher* cipher = nullptr;
    // Where the trees read through this session come from: each read is one arena from
    // it (docs/decisions.md D64), which must outlive the tree. Null: the default allocator.
    const Allocator* allocator = nullptr;

    struct State;  // defined in parse.cpp

private:
    friend class detail::Parser;
    friend struct detail::SessionAccess;
    friend Expected<Ref<Array>> read_string(std::string_view, std::string_view, LoadSession&);
    friend Expected<Ref<Array>> read_file(std::string_view, LoadSession&);

    State* st_;
};

[[nodiscard]] Expected<Ref<Array>> read_file(std::string_view path, LoadSession& session);
[[nodiscard]] Expected<Ref<Array>> read_file(std::string_view path);
// `name` is used as the file name in errors and as the base for relative includes.
[[nodiscard]] Expected<Ref<Array>> read_string(std::string_view text, std::string_view name,
                                               LoadSession& session);
[[nodiscard]] Expected<Ref<Array>> read_string(std::string_view text,
                                               std::string_view name = "<string>");

}  // namespace moira
