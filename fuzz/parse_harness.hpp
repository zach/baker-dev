#pragma once

#include <string_view>

namespace moira::fuzz {

// Parses `data` with no filesystem and tight limits. If it parses, prints the tree and
// parses the printout again. Returns false (a bug) if a successful parse does not
// round-trip, or if an error comes back without a usable location.
bool check_parse(std::string_view data);

}  // namespace moira::fuzz
