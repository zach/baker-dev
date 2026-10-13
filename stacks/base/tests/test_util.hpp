// Scratch directories and file writes for the base's tests.
#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace test {

// A fresh, empty directory under the system's temporary directory, removed on destruction.
struct ScratchDir {
    std::filesystem::path path;
    explicit ScratchDir(const char* name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               (std::string("stacks-") + name + "-" + std::to_string(stamp));
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

inline bool write(const std::filesystem::path& file, const std::string& text) {
    std::FILE* f = std::fopen(file.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

// Calls `poll` until it returns true or two seconds pass.
template <class F>
bool within_two_seconds(F&& poll) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < end) {
        if (poll()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

}  // namespace test
