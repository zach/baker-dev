#include <doctest.h>
#include <moira/script_info.hpp>
#include <moira/version.hpp>

TEST_CASE("script layer links against data layer") {
    CHECK(moira::script_linked_data_version() == moira::version());
}
