// main for moira_alloc_tests, in its own file: DOCTEST_CONFIG_IMPLEMENT brings in
// <windows.h>, whose TextOut macro would rename moira::TextOut in a file that uses it.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <moira/allocator.hpp>

#include <cstdlib>

int main(int argc, char** argv) {
    // Before Moira allocates anything: allocations with no owner go through malloc, never
    // operator new, so the tests can watch operator new alone.
    const moira::Allocator heap{
        [](void*, size_t size, size_t) -> void* { return std::malloc(size); },
        [](void*, void* p, size_t) { std::free(p); }, nullptr};
    if (!moira::set_default_allocator(heap)) return 2;
    doctest::Context context(argc, argv);
    return context.run();
}
