#pragma once

// Today's declarations in the spelling of the hash-declarations prototype (parse.hpp), line
// for line, so the tests' texts run on the core built with BAKER_HASH_DECLARATIONS: a
// first-form (pack name ...) is #pack (name ...), an (origin "source" ...) first or after it
// is #origin ("source" ...), a top-level (shapes (a ...) (b ...)) is #shape (a ...)
// #shape (b ...), and #define N (body), its body on the name's line, is [N body]. What today
// reads as data stays as it is. Without the switch, the text as it is.

#include <baker/text.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace baker::test {

inline std::string hash_spelled(std::string_view text) {
#if BAKER_HASH_DECLARATIONS
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    auto atom_end = [&](char c) {
        return space(c) || std::string_view("(){}[]\";").find(c) != std::string_view::npos;
    };
    std::string out;
    int depth = 0, forms = 0, shapes = -1, body = -1;  // a container's, a #define body's depth
    bool pack = false, origin = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const std::string_view rest = text.substr(i);
        const char c = rest[0];
        const bool token_start = i == 0 || atom_end(text[i - 1]);
        if (c == '"' || c == ';' || (c == '\'' && token_start)) {  // a string, quote, comment
            const size_t end = std::min(rest.find(c == ';' ? '\n' : c, 1), rest.size() - 1);
            out.append(rest.substr(0, end + 1));
            i += end;
            forms += depth == 0 && c != ';';
            continue;
        }
        if (depth == 0 && c == '(') {
            if (!pack && forms == 0 && rest.starts_with("(pack ")) {
                out += "#pack (";
                i += 5;
                pack = true;
                ++depth;
                continue;
            }
            if (!origin && forms == 0 && rest.starts_with("(origin \"")) {
                out += "#origin (";
                i += 7;
                origin = true;
                ++depth;
                continue;
            }
            if (rest.starts_with("(shapes") && rest.size() > 7 &&
                (space(rest[7]) || rest[7] == '(')) {
                out += "       ";
                i += 6;
                shapes = ++depth;
                continue;
            }
        }
        if (c == '#' && token_start && depth == 0) {
            size_t k = 1;
            while (k < rest.size() && !atom_end(rest[k])) ++k;
            const std::string_view directive = rest.substr(1, k - 1);
            if (directive == "include" || directive == "include_opt" || directive == "merge" ||
                directive == "require")
                ++forms;  // an included file may push nodes
            if (directive == "define") {
                size_t n = k;
                while (n < rest.size() && (rest[n] == ' ' || rest[n] == '\t')) ++n;
                size_t e = n;
                while (e < rest.size() && !atom_end(rest[e])) ++e;
                size_t p = e;
                while (p < rest.size() && (rest[p] == ' ' || rest[p] == '\t')) ++p;
                if (e > n && p < rest.size() && rest[p] == '(') {
                    out += "[" + std::string(rest.substr(n, e - n)) + " ";
                    i += p;
                    body = ++depth;
                    continue;
                }
            }
            out.append(rest.substr(0, k));
            i += k - 1;
            continue;
        }
        if (c == '(' || c == '{' || c == '[') {
            if (depth == shapes) out += "#shape ";
            forms += depth == 0 && c != '[';
            ++depth;
        } else if (c == ')' || c == '}' || c == ']') {
            if (depth == shapes || depth == body) {
                out += depth == body ? ']' : ' ';
                shapes = depth == shapes ? -1 : shapes;
                body = depth == body ? -1 : body;
                --depth;
                continue;
            }
            --depth;
        }
        out += c;
    }
    return out;
#else
    return std::string(text);
#endif
}

// A LoadSession reader of the disk's files, spelled as above.
inline bool read_spelled(std::string_view path, TextSink out) {
    std::ifstream in{std::string(path), std::ios::binary};
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    out(hash_spelled(text.str()));
    return true;
}

}  // namespace baker::test
