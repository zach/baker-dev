#include <moira/error.hpp>
#include <moira/std_interop.hpp>

#include <string>
#include <vector>

namespace moira {

namespace detail {
struct ErrorRecord {
    ErrorCode code = ErrorCode::Syntax;
    std::string message;
    Symbol file;
    int32_t line = 0;
    std::string path;
    std::vector<StackEntry> stack;
    // The formatted forms, made on first request and cleared by any change.
    std::string text;
    std::string described;
};
}  // namespace detail

namespace {
detail::ErrorRecord* copy_of(const detail::ErrorRecord* r) {
    return r ? new detail::ErrorRecord(*r) : nullptr;
}
}  // namespace

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

Error::Error(ErrorCode code, std::string_view message, Symbol file, int32_t line)
    : r_(new detail::ErrorRecord{code, std::string(message), file, line, {}, {}, {}, {}}) {}

Error::Error(const Error& other) : r_(copy_of(other.r_)) {}

Error& Error::operator=(const Error& other) {
    if (this != &other) {
        detail::ErrorRecord* r = copy_of(other.r_);
        delete r_;
        r_ = r;
    }
    return *this;
}

Error& Error::operator=(Error&& other) noexcept {
    if (this != &other) {
        delete r_;
        r_ = other.r_;
        other.r_ = nullptr;
    }
    return *this;
}

Error::~Error() {
    delete r_;
}

ErrorCode Error::code() const noexcept {
    return r_ ? r_->code : ErrorCode::Syntax;
}
std::string_view Error::message() const noexcept {
    return r_ ? std::string_view(r_->message) : std::string_view();
}
Symbol Error::file() const noexcept {
    return r_ ? r_->file : Symbol();
}
int32_t Error::line() const noexcept {
    return r_ ? r_->line : 0;
}
std::string_view Error::path() const noexcept {
    return r_ ? std::string_view(r_->path) : std::string_view();
}
std::span<const StackEntry> Error::stack() const noexcept {
    return r_ ? std::span<const StackEntry>(r_->stack) : std::span<const StackEntry>();
}

namespace {
detail::ErrorRecord& edit(detail::ErrorRecord*& r) {
    if (!r) r = new detail::ErrorRecord();
    r->text.clear();
    r->described.clear();
    return *r;
}
}  // namespace

void Error::set_message(std::string_view message) {
    edit(r_).message = message;
}
void Error::set_location(Symbol file, int32_t line) {
    detail::ErrorRecord& r = edit(r_);
    r.file = file;
    r.line = line;
}
void Error::set_path(std::string_view path) {
    edit(r_).path = path;
}
void Error::push_stack(const StackEntry& entry) {
    edit(r_).stack.push_back(entry);
}

Error make_error(ErrorCode code, std::string_view message, Symbol file, int32_t line) {
    return Error(code, message, file, line);
}

std::string_view to_string(const Error& e) {
    if (!e.r_) return "moira: no error";
    detail::ErrorRecord& r = *e.r_;
    if (!r.text.empty()) return r.text;
    std::string& out = r.text;
    out = "moira: ";
    out += r.message;
    const bool has_file = !r.file.empty();
    const bool has_path = !r.path.empty();
    if (has_file || has_path) {
        out += "  (";
        if (has_file) {
            out += r.file.str();
            if (r.line > 0) {
                out += ':';
                out += std::to_string(r.line);
            }
        }
        if (has_path) {
            if (has_file) out += ", ";
            out += "at ";
            out += r.path;
        }
        out += ')';
    }
    return out;
}

void write(const StackEntry& entry, TextOut out) {
    out("{");
    if (!entry.on.empty()) {
        out(entry.on.str());
        out(" ");
    }
    out(entry.function.empty() ? std::string_view("...") : entry.function.str());
    out("}");
    if (!entry.file.empty()) {
        out("  (");
        out(entry.file.str());
        if (entry.line > 0) {
            out(":");
            out(std::to_string(entry.line));
        }
        out(")");
    }
}

std::string_view describe(const Error& e) {
    if (!e.r_ || e.r_->stack.empty()) return to_string(e);
    detail::ErrorRecord& r = *e.r_;
    if (!r.described.empty()) return r.described;
    std::string out(to_string(e));
    for (const StackEntry& s : r.stack) {
        out += "\n  in ";
        write(s, string_out(out));
    }
    r.described = std::move(out);
    return r.described;
}

}  // namespace moira
