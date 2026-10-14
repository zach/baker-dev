// main for baker_alloc_tests, in its own file: DOCTEST_CONFIG_IMPLEMENT brings in
// <windows.h> and its macros, which stay out of the file with the tests.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <baker/allocator.hpp>

#include <cstdlib>

int main(int argc, char** argv) {
    // Before Baker allocates anything: allocations with no owner go through malloc, never
    // operator new, so the tests can watch operator new alone.
    const baker::Allocator heap{
        [](void*, size_t size, size_t) -> void* { return std::malloc(size); },
        [](void*, void* p, size_t) { std::free(p); }, nullptr};
    if (!baker::set_default_allocator(heap)) return 2;
    doctest::Context context(argc, argv);
    return context.run();
}
