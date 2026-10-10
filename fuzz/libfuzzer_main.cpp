// libFuzzer entry point. Build with -DMOIRA_BUILD_FUZZERS=ON (Clang), then e.g.
//   build/fuzz/moira_parse_fuzzer -dict=fuzz/dta.dict tests/corpus
#include "parse_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    if (!moira::fuzz::check_parse(text)) std::abort();
    return 0;
}
