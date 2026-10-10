#include <moira/script_info.hpp>
#include <moira/version.hpp>

namespace moira {

std::string_view script_linked_data_version() noexcept {
    return version();
}

}  // namespace moira
