#pragma once

// List<T>: an owning, fixed-size array, for results Moira hands back (a report's entries,
// a list of layers). Built once from a span and never resized; copying copies the items.
// Not a container to build with: Moira returns these, it does not ask for them.

#include <cstddef>
#include <span>
#include <utility>

namespace moira {

template <class T>
class List {
public:
    List() noexcept = default;
    explicit List(std::span<const T> items)
        : data_(items.empty() ? nullptr : new T[items.size()]), size_(items.size()) {
        for (size_t i = 0; i < size_; ++i) data_[i] = items[i];
    }
    List(const List& other) : List(other.items()) {}
    List(List&& other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }
    List& operator=(List other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        return *this;
    }
    ~List() { delete[] data_; }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] const T& operator[](size_t i) const noexcept { return data_[i]; }
    [[nodiscard]] const T* begin() const noexcept { return data_; }
    [[nodiscard]] const T* end() const noexcept { return data_ + size_; }
    [[nodiscard]] std::span<const T> items() const noexcept { return {data_, size_}; }

private:
    T* data_ = nullptr;
    size_t size_ = 0;
};

}  // namespace moira
