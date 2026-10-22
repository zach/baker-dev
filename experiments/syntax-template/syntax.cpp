#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

namespace baker::experiments::syntax {

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    return read_string(text, name);
}

std::string print(const Array& root) {
    return to_dta(root, {.indent = 3, .width = 96});
}

}  // namespace baker::experiments::syntax
