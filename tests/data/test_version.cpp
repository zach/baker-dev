#include <doctest.h>
#include <moira/version.hpp>

#include <string>

TEST_CASE("version string matches version constants") {
    const std::string expected = std::to_string(moira::kVersionMajor) + "." +
                                 std::to_string(moira::kVersionMinor) + "." +
                                 std::to_string(moira::kVersionPatch);
    CHECK(moira::version() == expected);
}
