#include "alloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

#if defined(__SANITIZE_THREAD__)
#define MOIRA_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MOIRA_TSAN 1
#endif
#endif

namespace {
std::atomic<size_t> g_allocations{0};
}  // namespace

size_t moira::test::allocations() noexcept {
    return g_allocations.load(std::memory_order_relaxed);
}

#if defined(MOIRA_TSAN)

bool moira::test::counting_allocations() noexcept {
    return false;
}

#else

bool moira::test::counting_allocations() noexcept {
    return true;
}

namespace {
void* counted_alloc(size_t n) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    std::abort();  // tests never run out of memory; avoids depending on exceptions
}
}  // namespace

void* operator new(size_t n) {
    return counted_alloc(n);
}
void* operator new[](size_t n) {
    return counted_alloc(n);
}
void* operator new(size_t n, const std::nothrow_t&) noexcept {
    return counted_alloc(n);
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept {
    return counted_alloc(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, size_t) noexcept {
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}

#endif
