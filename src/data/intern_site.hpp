#pragma once

// Where the names a thread interns come from, for a hash collision's report
// (set_symbol_collision_handler): the file a parser is reading and its current line, or the
// cache a load is reading. Set once per file, never per name; symbol.cpp keeps it.

#include <baker/error.hpp>
#include <baker/symbol.hpp>

#include <cstdint>

namespace baker::detail {

struct InternSite {
    Symbol file;
    const int32_t* line = nullptr;  // the reader's current line; null: none
};

// Sets this thread's site and returns the one it replaces, to put back.
InternSite set_intern_site(InternSite site) noexcept;

// The site for a scope, in a build that is not shipping; the outer one again after it.
class InternSiteScope {
public:
    explicit InternSiteScope(std::string_view file) {
        if constexpr (!kShipping) outer_ = set_intern_site({Symbol(file), nullptr});
    }
    ~InternSiteScope() {
        if constexpr (!kShipping) (void)set_intern_site(outer_);
    }
    InternSiteScope(const InternSiteScope&) = delete;
    InternSiteScope& operator=(const InternSiteScope&) = delete;

private:
    InternSite outer_;
};

}  // namespace baker::detail
