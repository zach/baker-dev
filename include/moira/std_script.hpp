#pragma once

// Opt-in interop between moira::script and the C++ standard library: the std::string forms
// of what moira::script writes through TextSink. The moira::script counterpart of
// <moira/std_interop.hpp>, kept apart so moira::data never includes script headers.

#include <moira/script.hpp>
#include <moira/std_interop.hpp>

#include <string>

namespace moira {

// The check report, one line each (moira::check).
inline std::string to_string(const CheckReport& report) {
    std::string out;
    write(report, string_out(out));
    return out;
}

// The script reference, as Markdown.
inline std::string reference(const Context& ctx) {
    std::string out;
    reference(ctx, string_out(out));
    return out;
}

// The script calls in progress on this thread's current Context, one line each.
inline std::string script_backtrace() {
    std::string out;
    script_backtrace(string_out(out));
    return out;
}

}  // namespace moira
