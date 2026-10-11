#include <moira/error.hpp>

#include <atomic>

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
        case ErrorCode::Refused: return "refused";
    }
    return "unknown";
}

Error make_error(ErrorCode code, std::string message, Symbol file, int32_t line) {
    return Error{code, std::move(message), file, line, {}, {}};
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

std::string to_string(const StackEntry& entry) {
    std::string out = "{";
    if (!entry.on.empty()) {
        out += entry.on.str();
        out += ' ';
    }
    out += entry.function.empty() ? std::string_view("...") : entry.function.str();
    out += '}';
    if (!entry.file.empty()) {
        out += "  (";
        out += entry.file.str();
        if (entry.line > 0) {
            out += ':';
            out += std::to_string(entry.line);
        }
        out += ')';
    }
    return out;
}

std::string describe(const Error& error) {
    std::string out = to_string(error);
    for (const StackEntry& e : error.stack) {
        out += "\n  in ";
        out += to_string(e);
    }
    return out;
}

namespace {
std::atomic<detail::StackRecorder> g_stack_recorder{nullptr};
}  // namespace

void detail::set_stack_recorder(StackRecorder recorder) noexcept {
    g_stack_recorder.store(recorder, std::memory_order_release);
}

#if MOIRA_EXCEPTIONS
Exception::Exception(Error error) : error_(std::move(error)) {
    if (const auto record = g_stack_recorder.load(std::memory_order_acquire)) record(error_);
    what_ = describe(error_);
}

namespace detail {
void raise(Error error) {
    throw Exception(std::move(error));
}
}  // namespace detail
#endif

}  // namespace moira
