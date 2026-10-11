#pragma once

// ok(expected): the value of a call that must succeed, or a test failure that shows the
// error with its script stack. Moira never throws (D54), so this is how tests say "this
// cannot fail" without a branch at every call.

#include <doctest.h>
#include <moira/error.hpp>

#include <utility>

namespace moira::test {

template <class T>
T ok(Expected<T> e) {
    if (!e) FAIL(describe(e.error()));
    return std::move(*e);
}

inline void ok(Expected<void> e) {
    if (!e) FAIL(describe(e.error()));
}

}  // namespace moira::test
