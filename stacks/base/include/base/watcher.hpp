// Watches directory trees for saved data files, so a game reloads only when the operating
// system says one changed, the frame after it did (game-stacks.md §2.2). It reads no file:
// Config::reload decides what actually changed, by content.
//
//   base::Watcher w;
//   w.watch("data");                  // the whole tree, subdirectories made later too
//   base::Changes c;                  // kept: polling allocates nothing when nothing changed
//   if (w.poll(c)) ...                // once a frame; never blocks
//
// ReadDirectoryChangesW on Windows, inotify on Linux. Only files with the watched
// extension count, so an editor's swap and backup files do not reload anything.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace base {

// A file's last write time, as nanoseconds in the file system's clock, offset so every
// time is positive (file clocks may count from before 1970). Live uses this clock as the
// Config's time source, so a reload's origin is when its edit was saved.
[[nodiscard]] uint64_t file_clock_ns(std::filesystem::file_time_type t) noexcept;
[[nodiscard]] uint64_t file_clock_now_ns() noexcept;

struct Changes {
    bool changed = false;
    // The newest last write time among the changed files that still exist, in
    // file_clock_ns; 0 when none could be read (all deleted, or the system's buffer
    // overflowed and the names were lost).
    uint64_t newest_write_ns = 0;
    std::vector<std::string> files;  // changed paths, as the system named them
};

class Watcher {
public:
    explicit Watcher(std::string extension = ".dta");
    ~Watcher();
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    // Watches `dir` and everything under it. False, with error() set, if it cannot.
    bool watch(const std::filesystem::path& dir);
    // Replaces `out` with everything that changed since the last poll; returns
    // out.changed. Never blocks.
    bool poll(Changes& out);
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    struct Impl;
    void note(const std::filesystem::path& file, Changes& out) const;

    std::string extension_;
    std::string error_;
    Impl* impl_ = nullptr;
};

}  // namespace base
