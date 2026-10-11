#include <moira/clock.hpp>

#include <chrono>

namespace moira {

uint64_t steady_now(void*) noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

}  // namespace moira
