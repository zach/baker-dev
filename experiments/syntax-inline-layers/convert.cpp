// #ifdef regions to layer blocks, on the text: a file keeps its comments and layout, and a
// region becomes a block only where a block means what the region meant.
#include "syntax.hpp"

#include <baker/parse.hpp>
#include <baker/std_interop.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace baker::experiments::syntax {

namespace {

// --- A scan of DTA text: brackets, atoms, strings and directives, with their offsets ---

struct Tok {
    enum Kind { Open, Close, Atom, String, Directive } kind;
    size_t begin, end;  // in the text
    int line;
    std::string_view text;  // an atom's or a directive's (without '#'); a bracket's char
};

bool atom_end(unsigned char c) {
    return c <= 0x20 ||
           std::string_view("(){}[]\";").find(static_cast<char>(c)) != std::string_view::npos;
}

// The tokens of `text`; comments are skipped and kept by the text itself. An unterminated
// string or comment runs to the end, as Baker's tolerant read takes it.
std::vector<Tok> scan(std::string_view text) {
    std::vector<Tok> out;
    size_t i = 0;
    int line = 1;
    const size_t n = text.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c <= 0x20) {
            line += c == '\n';
            ++i;
        } else if (c == ';') {
            while (i < n && text[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const size_t close = text.find("*/", i + 2);
            const size_t end = close == std::string_view::npos ? n : close + 2;
            line += static_cast<int>(std::count(text.begin() + i, text.begin() + end, '\n'));
            i = end;
        } else if (std::string_view("({[").find(static_cast<char>(c)) != std::string_view::npos) {
            out.push_back({Tok::Open, i, i + 1, line, text.substr(i, 1)});
            ++i;
        } else if (std::string_view(")}]").find(static_cast<char>(c)) != std::string_view::npos) {
            out.push_back({Tok::Close, i, i + 1, line, text.substr(i, 1)});
            ++i;
        } else if (c == '"' || c == '\'') {
            const size_t close = text.find(static_cast<char>(c), i + 1);
            const size_t end = close == std::string_view::npos ? n : close + 1;
            out.push_back(
                {c == '"' ? Tok::String : Tok::Atom, i, end, line, text.substr(i, end - i)});
            line += static_cast<int>(std::count(text.begin() + i, text.begin() + end, '\n'));
            i = end;
        } else {
            size_t j = i;
            while (j < n && !atom_end(static_cast<unsigned char>(text[j]))) ++j;
            const std::string_view atom = text.substr(i, j - i);
            if (atom.size() > 1 && atom[0] == '#')
                out.push_back({Tok::Directive, i, j, line, atom.substr(1)});
            else
                out.push_back({Tok::Atom, i, j, line, atom});
            i = j;
        }
    }
    return out;
}

// Whether an atom reads as a symbol (a tag), not a number or a variable.
bool is_symbol(std::string_view a) {
    if (a.empty() || a[0] == '$') return false;
    if (a[0] == '\'') return true;
    size_t k = (a[0] == '-' || a[0] == '+') ? 1 : 0;
    if (k < a.size() && a[k] == '.') ++k;
    return !(k < a.size() && std::isdigit(static_cast<unsigned char>(a[k])));
}

// --- Regions ---

struct Frame {
    char bracket;
    std::string tag;  // its first child, if a symbol
    int children = 0;
};

struct Branch {
    size_t begin = 0, end = 0;  // the text between the directives' lines
    bool ok = true;             // whole tagged forms, nothing else
};

struct Found {
    Region region;
    size_t from = 0, to = 0;        // the region's text, its directive lines whole
    Branch then, other;             // #ifdef's branch and #else's
    std::vector<std::string> tags;  // the tags enclosing it, outermost first
    size_t depth = 0;
};

// Where a directive's line begins, if only blanks are before it on its line; else the
// directive itself.
size_t line_start(std::string_view text, size_t at) {
    size_t k = at;
    while (k > 0 && (text[k - 1] == ' ' || text[k - 1] == '\t')) --k;
    return k == 0 || text[k - 1] == '\n' ? k : at;
}
// Past the end of a directive's line (its newline), if only blanks or a comment follow it;
// else the directive's end.
size_t line_end(std::string_view text, size_t at) {
    size_t k = at;
    while (k < text.size() && (text[k] == ' ' || text[k] == '\t' || text[k] == '\r')) ++k;
    if (k < text.size() && text[k] == ';')
        while (k < text.size() && text[k] != '\n') ++k;
    if (k == text.size()) return k;
    return text[k] == '\n' ? k + 1 : at;
}

std::vector<Found> find_regions(std::string_view text, const std::vector<Tok>& toks) {
    std::vector<Found> done;
    std::vector<Found> open;  // nested regions, innermost last
    std::vector<Frame> frames;
    // Marks the innermost open region's current branch: a token at its own depth, or a
    // close below it.
    auto branch = [&](Found& f) -> Branch& { return f.region.has_else ? f.other : f.then; };
    for (size_t i = 0; i < toks.size(); ++i) {
        const Tok& t = toks[i];
        if (t.kind == Tok::Directive &&
            (t.text == "ifdef" || t.text == "ifndef" || t.text == "else" || t.text == "endif")) {
            if (t.text == "ifdef" || t.text == "ifndef") {
                Found f;
                f.region.line = t.line;
                f.region.negated = t.text == "ifndef";
                size_t name_end = t.end;
                if (i + 1 < toks.size() && toks[i + 1].kind == Tok::Atom) {
                    f.region.define = std::string(toks[i + 1].text);
                    name_end = toks[i + 1].end;
                    ++i;
                }
                f.from = line_start(text, t.begin);
                f.then.begin = line_end(text, name_end);
                f.depth = frames.size();
                for (const Frame& fr : frames) {
                    if (fr.bracket != '(' || fr.tag.empty()) f.region.kind = Region::Kind::Untagged;
                    f.tags.push_back(fr.tag);
                }
                if (!open.empty()) {
                    f.region.kind = Region::Kind::Nested;
                    for (Found& o : open)
                        if (o.region.kind == Region::Kind::Converted)
                            o.region.kind = Region::Kind::HoldsDirective;
                }
                open.push_back(std::move(f));
            } else if (!open.empty()) {
                Found& f = open.back();
                if (frames.size() != f.depth) branch(f).ok = false;
                if (t.text == "else") {
                    f.then.end = line_start(text, t.begin);
                    f.other.begin = line_end(text, t.end);
                    f.region.has_else = true;
                } else {
                    branch(f).end = line_start(text, t.begin);
                    f.to = line_end(text, t.end);
                    if (!f.then.ok || !f.other.ok) {
                        if (f.region.kind == Region::Kind::Converted)
                            f.region.kind = Region::Kind::SpansBracket;
                    }
                    done.push_back(std::move(f));
                    open.pop_back();
                }
            }
            continue;
        }
        // Anything else inside an open region is judged against it.
        for (Found& f : open) {
            if (t.kind == Tok::Directive && t.text != "remove" &&
                f.region.kind == Region::Kind::Converted)
                f.region.kind = Region::Kind::HoldsDirective;
        }
        if (!open.empty()) {
            Found& f = open.back();
            if (t.kind == Tok::Close && frames.size() <= f.depth) branch(f).ok = false;
            if (frames.size() == f.depth && f.region.kind == Region::Kind::Converted) {
                // At the region's own level only tagged forms: '(' then a symbol.
                const bool form = t.kind == Tok::Open && t.text == "(" && i + 1 < toks.size() &&
                                  toks[i + 1].kind == Tok::Atom && is_symbol(toks[i + 1].text);
                if (t.kind != Tok::Close && !form) f.region.kind = Region::Kind::Values;
            }
        }
        if (t.kind == Tok::Open) {
            if (!frames.empty()) ++frames.back().children;
            frames.push_back({t.text[0], {}, 0});
        } else if (t.kind == Tok::Close) {
            if (!frames.empty()) frames.pop_back();
        } else if (!frames.empty()) {
            Frame& fr = frames.back();
            if (fr.children++ == 0 && t.kind == Tok::Atom && is_symbol(t.text))
                fr.tag = std::string(t.text);
        }
    }
    // A region never closed is left alone; Baker reports it.
    std::sort(done.begin(), done.end(),
              [](const Found& a, const Found& b) { return a.from < b.from; });
    return done;
}

// --- Keys a branch sets ---

void leaves(const Array& a, const std::string& at, std::set<std::string>& out) {
    bool values = a.size() <= 1;
    for (size_t i = 1; i < a.size(); ++i) {
        const Array* c = a[i].try_array();
        if (c && a[i].type() == Type::Array && !c->tag().empty())
            leaves(*c, at + "." + std::string(c->tag().str()), out);
        else
            values = true;
    }
    if (values) out.insert(at);
}

// The tag paths whose values a branch sets; false if the branch does not read alone.
bool branch_leaves(std::string_view text, std::set<std::string>& out) {
    LoadSession session;
    auto root = read_string(text, "branch.dta", session);
    if (!root) return false;
    for (const Node& n : **root)
        if (const Array* a = n.try_array(); a && !a->tag().empty())
            leaves(*a, std::string(a->tag().str()), out);
    return true;
}

bool subset(const std::set<std::string>& a, const std::set<std::string>& b) {
    return std::includes(b.begin(), b.end(), a.begin(), a.end());
}

// --- Writing blocks ---

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A branch's text, its common indentation removed and `indent` added, trailing blanks off.
std::string reindent(std::string_view text, size_t indent) {
    std::vector<std::string_view> lines;
    for (size_t at = 0; at <= text.size();) {
        size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos) nl = text.size();
        lines.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    auto blank = [](std::string_view l) {
        return l.find_first_not_of(" \t\r") == std::string_view::npos;
    };
    while (!lines.empty() && blank(lines.back())) lines.pop_back();
    while (!lines.empty() && blank(lines.front())) lines.erase(lines.begin());
    size_t common = std::string::npos;
    for (std::string_view l : lines)
        if (!blank(l)) common = std::min(common, l.find_first_not_of(" \t"));
    std::string out;
    for (std::string_view l : lines) {
        if (!out.empty()) out += '\n';
        if (blank(l)) continue;
        out += std::string(indent, ' ');
        out += l.substr(std::min(common, l.size()));
    }
    return out;
}

// Whether the last line of `text` ends in a comment, which a closing bracket after it would
// join.
bool ends_in_comment(std::string_view text) {
    const std::vector<Tok> toks = scan(text);
    size_t last = toks.empty() ? 0 : toks.back().end;
    return text.find(';', last) != std::string_view::npos ||
           text.find("*/", last) != std::string_view::npos;
}

}  // namespace

std::string_view describe(Region::Kind kind) {
    switch (kind) {
        case Region::Kind::Converted: return "converted";
        case Region::Kind::SpansBracket: return "spans a bracket";
        case Region::Kind::HoldsDirective: return "holds a directive";
        case Region::Kind::Nested: return "nested in another region";
        case Region::Kind::Values: return "holds values, not tagged forms";
        case Region::Kind::Untagged: return "inside an untagged array, command or property";
    }
    return "?";
}

Conversion to_blocks(std::string_view text) {
    Conversion out;
    const std::vector<Tok> toks = scan(text);
    std::vector<Found> found = find_regions(text, toks);

    // Each block's pieces, by condition, in the order first met: a piece is a branch in its
    // tags.
    struct Piece {
        std::vector<std::string> tags;
        std::string text;
    };
    std::vector<std::string> order;
    std::map<std::string, std::vector<Piece>> pieces;
    std::map<std::string, std::string> heads;  // condition -> (when ...) text
    auto add = [&](const Found& f, const Branch& b, bool when_defined) {
        const std::string key = (when_defined ? "" : "!") + f.region.define;
        if (!pieces.count(key)) {
            order.push_back(key);
            heads[key] = when_defined ? f.region.define : "(not " + f.region.define + ")";
        }
        pieces[key].push_back({f.tags, std::string(text.substr(b.begin, b.end - b.begin))});
    };

    std::string body;
    size_t at = 0;
    for (Found& f : found) {
        out.regions.push_back(f.region);
        if (f.region.kind != Region::Kind::Converted) continue;
        // The branch read when the define is set, and the one read when it is not.
        const Branch& yes = f.region.negated ? f.other : f.then;
        const Branch& no = f.region.negated ? f.then : f.other;
        const bool has_yes = !f.region.negated || f.region.has_else;
        const bool has_no = f.region.negated || f.region.has_else;
        std::string keep;  // what stays in the file, as the default
        if (has_yes && has_no) {
            std::set<std::string> ly, ln;
            const bool read = branch_leaves(text.substr(yes.begin, yes.end - yes.begin), ly) &&
                              branch_leaves(text.substr(no.begin, no.end - no.begin), ln);
            if (read && subset(ln, ly)) {  // the defined branch overrides all the other sets
                keep = std::string(text.substr(no.begin, no.end - no.begin));
                add(f, yes, true);
            } else if (read && subset(ly, ln)) {
                keep = std::string(text.substr(yes.begin, yes.end - yes.begin));
                add(f, no, false);
            } else {
                add(f, yes, true);
                add(f, no, false);
            }
        } else if (has_yes) {
            add(f, yes, true);
        } else {
            add(f, no, false);
        }
        body += text.substr(at, f.from - at);
        body += keep;
        at = f.to;
    }
    body += text.substr(at);
    if (order.empty()) {
        out.text = std::string(text);
        return out;
    }

    // One block per condition, after everything else: a block applies above its file.
    std::set<std::string> names;
    while (!body.empty() && (body.back() == '\n' || body.back() == ' ')) body.pop_back();
    body += "\n";
    for (const std::string& key : order) {
        const std::string base = lower(key[0] == '!' ? "not_" + key.substr(1) : key);
        std::string name = base;
        for (int k = 2; names.count(name); ++k) name = base + "_" + std::to_string(k);
        names.insert(name);
        out.blocks.push_back(name);
        std::string block = "\n(#layer (" + name + " (when " + heads[key] + "))";
        for (const Piece& p : pieces[key]) {
            std::string open, close;
            for (size_t d = 0; d < p.tags.size(); ++d) {
                open += "\n" + std::string(3 * (d + 1), ' ') + "(" + p.tags[d];
                close += ")";
            }
            const std::string forms = reindent(p.text, 3 * (p.tags.size() + 1));
            block += open + "\n" + forms;
            if (ends_in_comment(forms)) block += "\n" + std::string(3 * p.tags.size() + 3, ' ');
            block += close;
        }
        if (ends_in_comment(block)) block += "\n";
        block += ")\n";
        body += block;
    }
    out.text = std::move(body);
    return out;
}

std::vector<std::string> condition_defines(std::string_view text) {
    std::set<std::string> out;
    const std::vector<Tok> toks = scan(text);
    for (size_t i = 0; i + 1 < toks.size(); ++i) {
        const Tok& t = toks[i];
        if (t.kind == Tok::Directive && (t.text == "ifdef" || t.text == "ifndef") &&
            toks[i + 1].kind == Tok::Atom)
            out.insert(std::string(toks[i + 1].text));
        // (#layer (name (when COND) ...): the atoms of COND but its operators.
        if (t.kind == Tok::Directive && t.text == "layer" && toks[i + 1].kind == Tok::Open) {
            int depth = 0;
            bool in_when = false;
            int when_depth = 0;
            for (size_t k = i + 1; k < toks.size(); ++k) {
                const Tok& u = toks[k];
                if (u.kind == Tok::Open) {
                    ++depth;
                    if (k + 1 < toks.size() && toks[k + 1].text == "when" && !in_when) {
                        in_when = true;
                        when_depth = depth;
                        ++k;
                    }
                } else if (u.kind == Tok::Close) {
                    if (in_when && depth == when_depth) in_when = false;
                    if (--depth == 0) break;
                } else if (in_when && u.kind == Tok::Atom && u.text != "not" && u.text != "and" &&
                           u.text != "or") {
                    out.insert(std::string(u.text));
                }
            }
        }
    }
    return {out.begin(), out.end()};
}

}  // namespace baker::experiments::syntax
