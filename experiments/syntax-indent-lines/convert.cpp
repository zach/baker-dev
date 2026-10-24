// The migration tool (dev/syntax-indent-lines-design.md, Migration): today's text rewritten
// as lines, by deleting the parentheses of each array the layout reads back as the same
// array, so every comment, directive, macro and byte of layout the author wrote stays. The
// printer (syntax::print) writes a tree and keeps none of them.
//
// An array becomes a line when: it is parenthesized, closed, and tagged by a symbol that is
// not a macro; its '(' is first on its line, at the margin or at its parent line's body
// indent; its ')' ends its line (other closers aside); its parent is the root or a line
// that holds no value beyond its tag and ends with its tag; and every line of its own that begins
// inside it begins with a bracket, a macro or a directive, at one indent deeper than its own (an
// atom there continues its values, which a line would read as a child). A conditional
// takes no part, as in the layout. The result is read back and compared by the caller.
#include "syntax.hpp"

#include <baker/parse.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace baker::experiments::syntax {

namespace {

bool is_atom(TextTokenKind k) {
    return k == TextTokenKind::Int || k == TextTokenKind::Float || k == TextTokenKind::String ||
           k == TextTokenKind::Symbol || k == TextTokenKind::QuotedSymbol ||
           k == TextTokenKind::Variable;
}

}  // namespace

Conversion to_lines_text(std::string_view text) {
    const TextStructure s = read_structure(text, Symbol("convert.dta"));
    Conversion out;
    out.text = std::string(text);
    if (!s.errors.empty()) return out;  // a text Baker refuses is left as it is
    const auto& tok = s.tokens;
    const auto& br = s.brackets;
    const size_t n = tok.size();
    auto newline_between = [&](size_t from, size_t to) {
        return text.substr(from, to - from).find('\n') != std::string_view::npos;
    };
    std::vector<bool> first(n), last(n);
    std::vector<uint32_t> indent(n);
    for (size_t i = 0; i < n; ++i) {
        first[i] = i == 0 || newline_between(tok[i - 1].end, tok[i].begin);
        last[i] = i + 1 == n || newline_between(tok[i].end, tok[i + 1].begin);
        const size_t nl =
            tok[i].begin == 0 ? std::string_view::npos : text.rfind('\n', tok[i].begin - 1);
        indent[i] = tok[i].begin - static_cast<uint32_t>(nl == std::string_view::npos ? 0 : nl + 1);
    }
    // Each token's innermost bracket (-1: the root), each bracket's open and close tokens.
    std::vector<int32_t> owner(n, -1), open_tok(br.size(), -1), close_tok(br.size(), -1);
    {
        std::vector<int32_t> stack;
        size_t next = 0;  // brackets are listed in the order they open
        for (size_t i = 0; i < n; ++i) {
            owner[i] = stack.empty() ? -1 : stack.back();
            if (tok[i].kind == TextTokenKind::Open && next < br.size()) {
                open_tok[next] = static_cast<int32_t>(i);
                stack.push_back(static_cast<int32_t>(next++));
            } else if (tok[i].kind == TextTokenKind::Close && !stack.empty()) {
                close_tok[stack.back()] = static_cast<int32_t>(i);
                stack.pop_back();
            }
        }
    }
    // A directive's argument on its line is not a value.
    std::vector<bool> argument(n);
    for (size_t i = 0; i + 1 < n; ++i)
        if (tok[i].kind == TextTokenKind::Directive && !first[i + 1]) {
            const std::string_view d = text.substr(tok[i].begin + 1, tok[i].end - tok[i].begin - 1);
            argument[i + 1] = d != "remove";
        }
    auto conditional = [&](size_t i) {
        if (tok[i].kind != TextTokenKind::Directive) return false;
        const std::string_view d = text.substr(tok[i].begin, tok[i].end - tok[i].begin);
        return d == "#ifdef" || d == "#ifndef" || d == "#else" || d == "#endif";
    };
    // Per bracket: a value beyond its tag; its body's indent (UINT32_MAX: none yet).
    std::vector<bool> values(br.size()), line(br.size());
    std::vector<uint32_t> body(br.size(), UINT32_MAX);
    for (size_t i = 0; i < n; ++i)
        if (owner[i] >= 0 && is_atom(tok[i].kind) && !argument[i] &&
            static_cast<int32_t>(i) != br[owner[i]].tag)
            values[owner[i]] = true;
    for (size_t b = 0; b < br.size(); ++b) {
        if (br[b].open == '(') ++out.arrays;
        const int32_t o = open_tok[b], c = close_tok[b], p = br[b].parent;
        if (br[b].open != '(' || !br[b].closed || o < 0 || c < 0 || !first[o]) continue;
        ++out.first_on_line;
        auto kept = [&](size_t Conversion::*reason) {
            ++(out.*reason);
            return true;
        };
        if ((br[b].tag != o + 1 || tok[br[b].tag].kind == TextTokenKind::Macro || first[o + 1]) &&
            kept(&Conversion::untagged))
            continue;
        if (p >= 0 && !line[p] && kept(&Conversion::in_brackets)) continue;
        if (p >= 0 && values[p] && kept(&Conversion::under_values)) continue;
        // Its parent's nodes are all on the parent's line or all below it: a group's row
        // continued on the next line stays a row of groups.
        if (p >= 0 && !last[br[p].tag] && kept(&Conversion::beside_tag)) continue;
        auto tab = [&](size_t j) {
            return text.substr(tok[j].begin - indent[j], indent[j]).find('\t') !=
                   std::string_view::npos;
        };
        bool ok = !tab(o) && (p >= 0 ? indent[o] == body[p] : indent[o] == 0);
        // Its own lines: a bracket, a macro or a directive, at one indent deeper.
        bool continued = false;
        uint32_t own = UINT32_MAX;
        for (int32_t j = o + 1; j < c && ok && !continued; ++j) {
            if (owner[j] != static_cast<int32_t>(b) || !first[j] || conditional(j)) continue;
            continued = is_atom(tok[j].kind);
            if (own == UINT32_MAX) own = indent[j];
            ok = indent[j] == own && own > indent[o] && !tab(j);
        }
        if (continued && kept(&Conversion::continued)) continue;
        // At the top level, the next line that begins there begins at the margin.
        for (size_t j = c + 1; j < n && ok && p < 0; ++j)
            if (owner[j] == -1 && first[j] && !conditional(j)) {
                ok = indent[j] == 0;
                break;
            }
        if (!ok && kept(&Conversion::layout)) continue;
        ok = true;  // its ')' ends its line, closers aside
        for (size_t j = c + 1; j < n && !first[j] && ok; ++j)
            ok = tok[j].kind == TextTokenKind::Close;
        if (!ok && kept(&Conversion::shares_line)) continue;
        line[b] = true;
        body[b] = own;
        ++out.lines;
    }
    // The deletions, last first: each line's '(' and ')', and a line its ')' leaves empty.
    std::vector<std::pair<uint32_t, uint32_t>> cuts;
    for (size_t b = 0; b < br.size(); ++b) {
        if (!line[b]) continue;
        cuts.emplace_back(tok[open_tok[b]].begin, tok[open_tok[b]].end);
        const TextToken& close = tok[close_tok[b]];
        cuts.emplace_back(close.begin, close.end);
        // Rows aligned under the node after its tag move left with it: one space a line.
        const int32_t o = open_tok[b], after = o + 2;
        if (after < close_tok[b] && !first[after] && body[b] != UINT32_MAX &&
            body[b] == indent[o] + (tok[after].begin - tok[o].begin))
            for (uint32_t at = tok[o].end; at < close.begin; ++at)
                if (text[at] == '\n' && at + 1 < text.size() && text[at + 1] == ' ')
                    cuts.emplace_back(at + 1, at + 2);
    }
    std::sort(cuts.begin(), cuts.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [from, to] : cuts) {
        out.text.erase(from, to - from);
        // A line left blank by the cut goes with it.
        const size_t start = from == 0 ? 0 : out.text.rfind('\n', from - 1) + 1;
        size_t end = out.text.find('\n', from);
        if (end == std::string::npos) end = out.text.size();
        if (out.text.find_first_not_of(" \t\r", start) >= end && start > 0)
            out.text.erase(start - 1, end - start + 1);
    }
    return out;
}

}  // namespace baker::experiments::syntax
