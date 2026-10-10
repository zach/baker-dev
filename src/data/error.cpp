#include <moira/error.hpp>

namespace moira {

std::string_view error_code_name(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Syntax: return "syntax";
        case ErrorCode::Range: return "range";
        case ErrorCode::Directive: return "directive";
        case ErrorCode::Io: return "io";
        case ErrorCode::Limit: return "limit";
        case ErrorCode::Type: return "type";
        case ErrorCode::NotFound: return "not found";
    }
    return "unknown";
}

Error make_error(ErrorCode code, std::string message, Symbol file, int32_t line) {
    return Error{code, std::move(message), file, line, {}};
}

std::string to_string(const Error& e) {
    std::string out = "moira: ";
    out += e.message;
    const bool has_file = !e.file.empty();
    const bool has_path = !e.path.empty();
    if (has_file || has_path) {
        out += "  (";
        if (has_file) {
            out += e.file.str();
            if (e.line > 0) {
                out += ':';
                out += std::to_string(e.line);
            }
        }
        if (has_path) {
            if (has_file) out += ", ";
            out += "at ";
            out += e.path;
        }
        out += ')';
    }
    return out;
}

#if MOIRA_EXCEPTIONS
Exception::Exception(Error error) : error_(std::move(error)), what_(to_string(error_)) {}

namespace detail {
void raise(Error error) {
    throw Exception(std::move(error));
}
}  // namespace detail
#endif

}  // namespace moira
