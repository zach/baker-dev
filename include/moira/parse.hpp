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
// A macro name written as a bare symbol is replaced by the macro's nodes. Expansion
// shares arrays instead of copying them. Paths may be "quoted" or bare.
//
// Nothing in a file can crash the reader: malformed input yields a located Error, and
// ParseLimits bounds nesting, includes, and total size.

#include <moira/array.hpp>
#include <moira/error.hpp>
#include <moira/source_map.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace moira {

namespace detail {
class Parser;
}

struct ParseLimits {
    uint32_t max_depth = 256;                // array nesting, including spliced macro arrays
    uint32_t max_include_depth = 32;         // #include / #merge nesting
    uint32_t max_includes = 4096;            // files opened per top-level read
    uint64_t max_nodes = uint64_t{1} << 26;  // nodes created per top-level read
};

// State shared by all files read through it. Macros and defines persist from one file to
// the next, as in the original; a fresh session starts empty.
class LoadSession {
public:
    // Reads the whole file at `path` into `out`; returns false if it cannot.
    using Reader = std::function<bool(const std::string& path, std::string& out)>;

    LoadSession();

    void define(Symbol name);                   // a flag, for #ifdef
    void define(Symbol name, Ref<Array> body);  // a macro; null body means a flag
    void undefine(Symbol name);
    [[nodiscard]] bool is_defined(Symbol name) const;
    [[nodiscard]] const Array* macro(Symbol name) const;  // null unless a macro

    // Replaces file access (default: the local filesystem), e.g. to serve files from
    // memory or a socket.
    void set_reader(Reader reader);
    [[nodiscard]] const Reader& reader() const noexcept { return reader_; }

    std::vector<std::string> include_dirs;  // searched after the including file's directory
    ParseLimits limits;
    std::vector<Node> autorun;  // #autorun commands, in load order
    // Set for an edit-mode parse: records source text and node spans (source_map.hpp).
    // Null (the default) is a gameplay parse, which keeps only the tree.
    SourceMap* source_map = nullptr;

private:
    friend class detail::Parser;
    friend Expected<Ref<Array>> try_read_string(std::string_view, std::string_view, LoadSession&);
    friend Expected<Ref<Array>> try_read_file(std::string_view, LoadSession&);

    std::unordered_map<Symbol, Ref<Array>> defines_;
    Reader reader_;
    // Per top-level read.
    std::vector<std::string> include_stack_;
    uint64_t nodes_ = 0;
    uint32_t includes_ = 0;
};

[[nodiscard]] Expected<Ref<Array>> try_read_file(std::string_view path, LoadSession& session);
[[nodiscard]] Expected<Ref<Array>> try_read_file(std::string_view path);
// `name` is used as the file name in errors and as the base for relative includes.
[[nodiscard]] Expected<Ref<Array>> try_read_string(std::string_view text, std::string_view name,
                                                   LoadSession& session);
[[nodiscard]] Expected<Ref<Array>> try_read_string(std::string_view text,
                                                   std::string_view name = "<string>");

#if MOIRA_EXCEPTIONS
[[nodiscard]] Ref<Array> read_file(std::string_view path, LoadSession& session);
[[nodiscard]] Ref<Array> read_file(std::string_view path);
[[nodiscard]] Ref<Array> read_string(std::string_view text, std::string_view name,
                                     LoadSession& session);
[[nodiscard]] Ref<Array> read_string(std::string_view text, std::string_view name = "<string>");
#endif

}  // namespace moira
