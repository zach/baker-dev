#pragma once

// Placeholder for the script layer until M4. Exists so moira::script is a real target
// whose dependency on moira::data is exercised by the build.

#include <string_view>

namespace moira {

// Version of moira::data as seen from moira::script; must equal version().
[[nodiscard]] std::string_view script_linked_data_version() noexcept;

}  // namespace moira
