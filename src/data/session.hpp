#pragma once

// What the binary cache reads from and replays into a LoadSession (cache.cpp, D72).

#include <moira/parse.hpp>

#include "common/containers.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace moira::detail {

// A file a recorded load read, or looked for and did not find (size -1).
struct CacheFile {
    Str path;
    int64_t size;
    uint64_t hash;
};

// The cache's checksum, also of the files a load read (cache.cpp).
[[nodiscard]] uint64_t content_hash(std::string_view bytes) noexcept;
// The cache at `path`, replayed on the session, if it loads and is fresh; else null.
[[nodiscard]] Ref<Array> read_fresh_cache(std::string_view path, LoadSession& session);

struct SessionAccess {
    using Defines = HashMap<Symbol, Ref<Array>>;
    [[nodiscard]] static bool reads_disk(const LoadSession& s) noexcept;
    // Whether the last top-level read was recorded (LoadSession::record_for_cache).
    [[nodiscard]] static bool recorded(const LoadSession& s) noexcept;
    [[nodiscard]] static std::span<const CacheFile> files(const LoadSession& s) noexcept;
    [[nodiscard]] static const Defines& defines(const LoadSession& s) noexcept;
    [[nodiscard]] static const Defines& defines_before(const LoadSession& s) noexcept;
    // The #autorun commands the last top-level read added.
    [[nodiscard]] static std::span<const Node> new_autorun(const LoadSession& s) noexcept;
    static void add_autorun(LoadSession& s, Node command);
};

}  // namespace moira::detail
