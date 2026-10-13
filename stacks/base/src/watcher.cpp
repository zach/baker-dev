#include "base/watcher.hpp"

#include <chrono>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <unordered_map>
#else
#error "base::Watcher supports Windows and Linux"
#endif

namespace fs = std::filesystem;

namespace base {

uint64_t file_clock_ns(fs::file_time_type t) noexcept {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch());
    return static_cast<uint64_t>(ns.count()) + (uint64_t{1} << 62);
}

uint64_t file_clock_now_ns() noexcept {
    return file_clock_ns(fs::file_time_type::clock::now());
}

void Watcher::note(const fs::path& file, Changes& out) const {
    if (file.extension() != extension_) return;
    out.changed = true;
    out.files.push_back(file.string());
    std::error_code ec;
    const auto t = fs::last_write_time(file, ec);
    if (!ec && file_clock_ns(t) > out.newest_write_ns) out.newest_write_ns = file_clock_ns(t);
}

#ifdef _WIN32

// One directory handle per watched tree, with an overlapped ReadDirectoryChangesW always
// outstanding; poll() collects a completed one and issues the next.
struct Watcher::Impl {
    struct Tree {
        fs::path root;
        HANDLE dir = INVALID_HANDLE_VALUE;
        OVERLAPPED overlapped{};
        alignas(DWORD) unsigned char buffer[64 * 1024];
    };
    std::vector<Tree*> trees;

    static bool issue(Tree& t) {
        t.overlapped = OVERLAPPED{};
        return ReadDirectoryChangesW(t.dir, t.buffer, sizeof t.buffer, TRUE,
                                     FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                         FILE_NOTIFY_CHANGE_SIZE,
                                     nullptr, &t.overlapped, nullptr) != 0;
    }
    ~Impl() {
        for (Tree* t : trees) {
            CancelIoEx(t->dir, &t->overlapped);
            DWORD bytes = 0;
            GetOverlappedResult(t->dir, &t->overlapped, &bytes, TRUE);  // let the cancel land
            CloseHandle(t->dir);
            delete t;
        }
    }
};

Watcher::Watcher(std::string extension) : extension_(std::move(extension)), impl_(new Impl) {}

Watcher::~Watcher() {
    delete impl_;
}

bool Watcher::watch(const fs::path& dir) {
    auto* t = new Impl::Tree;
    t->root = dir;
    t->dir = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (t->dir == INVALID_HANDLE_VALUE || !Impl::issue(*t)) {
        error_ = "cannot watch " + dir.string() + ": error " + std::to_string(GetLastError());
        if (t->dir != INVALID_HANDLE_VALUE) CloseHandle(t->dir);
        delete t;
        return false;
    }
    impl_->trees.push_back(t);
    return true;
}

bool Watcher::poll(Changes& out) {
    out.changed = false;
    out.newest_write_ns = 0;
    out.files.clear();
    for (Impl::Tree* t : impl_->trees) {
        DWORD bytes = 0;
        if (!GetOverlappedResult(t->dir, &t->overlapped, &bytes, FALSE)) {
            if (GetLastError() != ERROR_IO_INCOMPLETE) Impl::issue(*t);
            continue;
        }
        if (bytes == 0) {
            out.changed = true;  // the buffer overflowed: something changed, names lost
        } else {
            for (const unsigned char* p = t->buffer;;) {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
                // Every action counts: a file removed or renamed away changes the data too.
                note(t->root / std::wstring(info->FileName, info->FileNameLength / sizeof(WCHAR)),
                     out);
                if (info->NextEntryOffset == 0) break;
                p += info->NextEntryOffset;
            }
        }
        Impl::issue(*t);
    }
    return out.changed;
}

#else  // Linux

// inotify watches one directory, not a tree: every directory under a watched root gets a
// watch, and so does each directory made later, as it appears.
struct Watcher::Impl {
    int fd = -1;
    std::unordered_map<int, fs::path> dirs;  // watch descriptor to directory

    bool add(const fs::path& dir) {
        const int wd = inotify_add_watch(fd, dir.c_str(),
                                         IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
        if (wd < 0) return false;
        dirs[wd] = dir;
        return true;
    }
    bool add_tree(const fs::path& root) {
        if (!add(root)) return false;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec) && !add(it->path())) return false;
        return true;
    }
    ~Impl() {
        if (fd >= 0) close(fd);
    }
};

Watcher::Watcher(std::string extension) : extension_(std::move(extension)), impl_(new Impl) {
    impl_->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (impl_->fd < 0) error_ = "inotify_init1 failed";
}

Watcher::~Watcher() {
    delete impl_;
}

bool Watcher::watch(const fs::path& dir) {
    if (impl_->fd < 0) return false;
    if (!impl_->add_tree(dir)) {
        error_ = "cannot watch " + dir.string();
        return false;
    }
    return true;
}

bool Watcher::poll(Changes& out) {
    out.changed = false;
    out.newest_write_ns = 0;
    out.files.clear();
    alignas(inotify_event) char buffer[16 * 1024];
    for (;;) {
        const ssize_t n = read(impl_->fd, buffer, sizeof buffer);
        if (n <= 0) break;  // EAGAIN: nothing more
        for (ssize_t at = 0; at < n;) {
            const auto* e = reinterpret_cast<const inotify_event*>(buffer + at);
            at += static_cast<ssize_t>(sizeof(inotify_event) + e->len);
            if (e->mask & IN_Q_OVERFLOW) {
                out.changed = true;
                continue;
            }
            const auto dir = impl_->dirs.find(e->wd);
            if (dir == impl_->dirs.end() || e->len == 0) continue;
            const fs::path path = dir->second / e->name;
            if (e->mask & IN_ISDIR) {
                if (e->mask & IN_CREATE) impl_->add_tree(path);
                continue;
            }
            // A new file shows as IN_CREATE, then IN_CLOSE_WRITE once written; a deleted one
            // as IN_DELETE. Saving in place or by renaming over the old file is
            // IN_CLOSE_WRITE or IN_MOVED_TO.
            if (e->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE)) note(path, out);
        }
    }
    return out.changed;
}

#endif

}  // namespace base
