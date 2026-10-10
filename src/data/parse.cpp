#include <moira/parse.hpp>

#include "text.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

namespace moira {
namespace detail {
namespace {

enum class Tok : uint8_t { End, Open, Close, Int, Float, String, Symbol, Var, Directive };

struct Token {
    Tok kind = Tok::End;
    char bracket = 0;     // Open, Close
    bool quoted = false;  // Symbol written as 'text'
    int32_t line = 0;
    int32_t i = 0;
    float f = 0;
    std::string_view text;  // symbol, var name, directive name, decoded string
};

std::string_view bracket_name(char c) noexcept {
    switch (c) {
        case '(': return "'('";
        case ')': return "')'";
        case '{': return "'{'";
        case '}': return "'}'";
        case '[': return "'['";
        case ']': return "']'";
    }
    return "?";
}

class Lexer {
public:
    Lexer(std::string_view src, Symbol file) : src_(src), file_(file) {
        if (src_.size() >= 3 && std::memcmp(src_.data(), "\xEF\xBB\xBF", 3) == 0) pos_ = 3;
    }

    // Produces the next token. On failure fills `err` and returns false.
    bool next(Token& t, Error& err) {
        if (has_peek_) {
            has_peek_ = false;
            t = peeked_;
            return true;
        }
        return lex(t, err);
    }
    bool peek(Token& t, Error& err) {
        if (!has_peek_) {
            if (!lex(peeked_, err)) return false;
            has_peek_ = true;
        }
        t = peeked_;
        return true;
    }

private:
    bool fail(Error& err, ErrorCode code, std::string msg, int32_t line) {
        err = make_error(code, std::move(msg), file_, line);
        return false;
    }

    bool lex(Token& t, Error& err) {
        t = Token{};
        const size_t n = src_.size();
        for (;;) {
            if (pos_ >= n) {
                t.kind = Tok::End;
                t.line = line_;
                return true;
            }
            const auto c = static_cast<unsigned char>(src_[pos_]);
            if (c <= 0x20) {
                if (c == '\n') ++line_;
                ++pos_;
            } else if (c == ';') {
                while (pos_ < n && src_[pos_] != '\n') ++pos_;
            } else if (c == '/' && pos_ + 1 < n && src_[pos_ + 1] == '*') {
                const int32_t start = line_;
                pos_ += 2;
                for (;;) {
                    if (pos_ + 1 >= n)
                        return fail(err, ErrorCode::Syntax, "unterminated /* comment", start);
                    if (src_[pos_] == '*' && src_[pos_ + 1] == '/') {
                        pos_ += 2;
                        break;
                    }
                    if (src_[pos_] == '\n') ++line_;
                    ++pos_;
                }
            } else {
                break;
            }
        }

        t.line = line_;
        const char c = src_[pos_];
        switch (c) {
            case '(':
            case '{':
            case '[':
                t.kind = Tok::Open;
                t.bracket = c;
                ++pos_;
                return true;
            case ')':
            case '}':
            case ']':
                t.kind = Tok::Close;
                t.bracket = c;
                ++pos_;
                return true;
            case '"': return lex_string(t, err);
            case '\'': return lex_quoted_symbol(t, err);
            default: return lex_atom(t, err);
        }
    }

    // "..." with two escapes: \n (newline) and \q (double quote). Any other backslash is
    // literal, so Windows paths need no doubling.
    bool lex_string(Token& t, Error& err) {
        const int32_t start_line = line_;
        const size_t begin = ++pos_;
        const size_t n = src_.size();
        size_t i = begin;
        bool escapes = false;
        while (i < n && src_[i] != '"') {
            if (src_[i] == '\\' && i + 1 < n && (src_[i + 1] == 'n' || src_[i + 1] == 'q')) {
                escapes = true;
                i += 2;
                continue;
            }
            ++i;
        }
        if (i >= n) return fail(err, ErrorCode::Syntax, "unterminated string", start_line);
        const std::string_view raw = src_.substr(begin, i - begin);
        for (char ch : raw)
            if (ch == '\n') ++line_;
        pos_ = i + 1;
        t.kind = Tok::String;
        if (!escapes) {
            t.text = raw;
            return true;
        }
        buf_.clear();
        for (size_t k = 0; k < raw.size(); ++k) {
            if (raw[k] == '\\' && k + 1 < raw.size() && (raw[k + 1] == 'n' || raw[k + 1] == 'q')) {
                buf_ += raw[k + 1] == 'n' ? '\n' : '"';
                ++k;
            } else {
                buf_ += raw[k];
            }
        }
        t.text = buf_;
        return true;
    }

    bool lex_quoted_symbol(Token& t, Error& err) {
        const int32_t start_line = line_;
        const size_t begin = ++pos_;
        const size_t end = src_.find('\'', begin);
        if (end == std::string_view::npos)
            return fail(err, ErrorCode::Syntax, "unterminated 'quoted symbol'", start_line);
        t.kind = Tok::Symbol;
        t.quoted = true;
        t.text = src_.substr(begin, end - begin);
        for (char ch : t.text)
            if (ch == '\n') ++line_;
        pos_ = end + 1;
        return true;
    }

    bool lex_atom(Token& t, Error& err) {
        const size_t begin = pos_;
        while (pos_ < src_.size() && !is_atom_end(static_cast<unsigned char>(src_[pos_]))) ++pos_;
        const std::string_view atom = src_.substr(begin, pos_ - begin);
        if (atom[0] == '#') {
            t.kind = Tok::Directive;
            t.text = atom.substr(1);
            return true;
        }
        if (atom[0] == '$') {
            if (atom.size() == 1)
                return fail(err, ErrorCode::Syntax, "'$' without a variable name", t.line);
            t.kind = Tok::Var;
            t.text = atom.substr(1);
            return true;
        }
        const Number num = classify_number(atom);
        switch (num.kind) {
            case NumberKind::Int:
                t.kind = Tok::Int;
                t.i = num.i;
                return true;
            case NumberKind::Float:
                t.kind = Tok::Float;
                t.f = num.f;
                return true;
            case NumberKind::IntOutOfRange:
                return fail(err, ErrorCode::Range,
                            "integer " + std::string(atom) + " is out of 32-bit range", t.line);
            case NumberKind::FloatOutOfRange:
                return fail(err, ErrorCode::Range,
                            "float " + std::string(atom) + " is out of float range", t.line);
            case NumberKind::NotNumber: break;
        }
        t.kind = Tok::Symbol;
        t.text = atom;
        return true;
    }

    std::string_view src_;
    Symbol file_;
    size_t pos_ = 0;
    int32_t line_ = 1;
    std::string buf_;  // decoded string with escapes; valid until the next string token
    bool has_peek_ = false;
    Token peeked_;
};

std::string normalize_path(const std::filesystem::path& p) {
    return p.lexically_normal().generic_string();
}

bool read_disk_file(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, got);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

// Fill-missing merge (#merge): for each tagged array in `src`, merge it into the array in
// dst[first..] with the same symbol tag, or append it if there is none. Existing values
// win; non-array nodes of `src` are ignored; untagged arrays are appended. Arrays along
// the way are rebuilt, never mutated, because they may be shared by macros. Returns
// whether `dst` changed.
//
// The GDC 2005 slide shows (a (b 1)) merged with (a (b 2) (c 2)) giving (a (b 1) (c 2)).
// The spec took that for a typo; it is not. See docs/decisions.md D1.
bool merge_missing_into(std::vector<Node>& dst, size_t first, const Array& src) {
    bool changed = false;
    for (const Node& s : src) {
        const Array* sa = s.try_array();
        if (!sa) continue;
        const auto tag = sa->empty() ? std::nullopt : (*sa)[0].try_sym();
        size_t found = dst.size();
        if (tag) {
            for (size_t i = first; i < dst.size(); ++i) {
                const Array* da = dst[i].try_array();
                if (da && !da->empty() && (*da)[0].try_sym() == tag) {
                    found = i;
                    break;
                }
            }
        }
        if (found == dst.size()) {
            dst.push_back(s);
            changed = true;
            continue;
        }
        const Array& da = *dst[found].try_array();
        std::vector<Node> merged(da.begin(), da.end());
        if (merge_missing_into(merged, 0, *sa)) {
            dst[found] = Node(Array::make_from(merged, da.file(), da.line()));
            changed = true;
        }
    }
    return changed;
}

}  // namespace

class Parser {
public:
    Parser(LoadSession& session, std::string_view text, Symbol file, uint32_t include_depth)
        : s_(session), lex_(text, file), file_(file), include_depth_(include_depth) {}

    bool run(Ref<Array>& out, Error& err) {
        err_ = &err;
        frames_.push_back({Frame::Root, 0, 1, {}});
        Token t;
        for (;;) {
            if (!lex_.next(t, err)) return false;
            line_ = t.line;
            if (t.kind == Tok::End) break;
            if (!active()) {
                if (t.kind == Tok::Directive && !skipped_directive(t)) return false;
                continue;
            }
            bool ok = true;
            switch (t.kind) {
                case Tok::Open: ok = open(t.bracket, t.line); break;
                case Tok::Close: ok = close(t); break;
                case Tok::Int: ok = push(Node(t.i)); break;
                case Tok::Float: ok = push(Node(t.f)); break;
                case Tok::String: ok = push(Node(String::make(t.text))); break;
                case Tok::Symbol: ok = symbol(t); break;
                case Tok::Var: ok = push(Node::var(Symbol(t.text))); break;
                case Tok::Directive: ok = directive(t); break;
                case Tok::End: break;
            }
            if (!ok) return false;
        }
        if (!conds_.empty())
            return fail(ErrorCode::Directive,
                        "#" + std::string(conds_.back().negated ? "ifndef" : "ifdef") +
                            " has no matching #endif",
                        conds_.back().line);
        if (frames_.size() > 1) {
            const FrameInfo& f = frames_.back();
            return fail(ErrorCode::Syntax, std::string(bracket_name(f.open)) + " is never closed",
                        f.line);
        }
        Ref<Array> root = Array::make_from(scratch_, file_, 1);
        if (root->height() > s_.limits.max_depth + 1) return too_deep(1);
        out = std::move(root);
        return true;
    }

private:
    enum class Frame : uint8_t { Root, Array, Command, Property, MacroDef, DefineBody, Autorun };
    struct FrameInfo {
        Frame kind;
        size_t start;
        int32_t line;
        Symbol name;  // DefineBody
        char open = '(';
    };
    struct Cond {
        bool active;         // tokens in the current branch are read
        bool parent_active;  // the enclosing region is read
        bool value;          // the condition's result
        bool seen_else;
        bool negated;
        int32_t line;
    };

    bool fail(ErrorCode code, std::string msg, int32_t line) {
        *err_ = make_error(code, std::move(msg), file_, line);
        return false;
    }
    bool too_deep(int32_t line) {
        return fail(ErrorCode::Limit,
                    "nesting deeper than " + std::to_string(s_.limits.max_depth) + " levels", line);
    }
    [[nodiscard]] bool active() const noexcept { return conds_.empty() || conds_.back().active; }

    bool push(Node n) {
        if (++s_.nodes_ > s_.limits.max_nodes)
            return fail(ErrorCode::Limit,
                        "more than " + std::to_string(s_.limits.max_nodes) + " nodes", line_);
        if (frames_.back().kind == Frame::Root && n.type() == Type::Property)
            return fail(ErrorCode::Syntax,
                        "a [property] cannot appear at top level, where '[' defines a macro",
                        line_);
        scratch_.push_back(std::move(n));
        return true;
    }

    bool symbol(const Token& t) {
        const Symbol sym(t.text);
        const FrameInfo& f = frames_.back();
        const bool naming_macro = f.kind == Frame::MacroDef && scratch_.size() == f.start;
        if (!t.quoted && !naming_macro && !s_.defines_.empty()) {
            if (const Array* body = s_.macro(sym)) {
                for (const Node& n : *body)
                    if (!push(n)) return false;
                return true;
            }
        }
        return push(Node(sym));
    }

    bool open(char bracket, int32_t line) {
        if (frames_.size() > s_.limits.max_depth) return too_deep(line);
        Frame kind = Frame::Array;
        if (bracket == '{') kind = Frame::Command;
        if (bracket == '[')
            kind = frames_.back().kind == Frame::Root ? Frame::MacroDef : Frame::Property;
        frames_.push_back({kind, scratch_.size(), line, {}, bracket});
        return true;
    }

    static char closer(Frame k) noexcept {
        switch (k) {
            case Frame::Command:
            case Frame::Autorun: return '}';
            case Frame::Property:
            case Frame::MacroDef: return ']';
            default: return ')';
        }
    }

    bool close(const Token& t) {
        const FrameInfo f = frames_.back();
        if (f.kind == Frame::Root)
            return fail(ErrorCode::Syntax, "unexpected " + std::string(bracket_name(t.bracket)),
                        t.line);
        if (t.bracket != closer(f.kind))
            return fail(ErrorCode::Syntax,
                        std::string(bracket_name(f.open)) + " from line " + std::to_string(f.line) +
                            " is closed by " + std::string(bracket_name(t.bracket)),
                        t.line);
        frames_.pop_back();
        std::span<Node> kids(scratch_.data() + f.start, scratch_.size() - f.start);

        if (f.kind == Frame::MacroDef) {
            const auto name = kids.empty() ? std::nullopt : kids[0].try_sym();
            if (!name)
                return fail(ErrorCode::Syntax, "macro definition needs a symbol name", f.line);
            Ref<Array> body = Array::make_from(kids.subspan(1), file_, f.line);
            scratch_.resize(f.start);
            if (body->height() > s_.limits.max_depth) return too_deep(f.line);
            s_.define(*name, std::move(body));
            return true;
        }

        Ref<Array> arr = Array::make_from(kids, file_, f.line);
        scratch_.resize(f.start);
        if (arr->height() > s_.limits.max_depth) return too_deep(f.line);
        switch (f.kind) {
            case Frame::DefineBody: s_.define(f.name, std::move(arr)); return true;
            case Frame::Autorun:
                s_.autorun.emplace_back(std::move(arr), Type::Command);
                return true;
            case Frame::Command: return push(Node(std::move(arr), Type::Command));
            case Frame::Property: return push(Node(std::move(arr), Type::Property));
            default: return push(Node(std::move(arr)));
        }
    }

    // Reads the symbol argument of a directive, unexpanded.
    bool name_arg(std::string_view directive, Token& t) {
        if (!lex_.next(t, *err_)) return false;
        if (t.kind != Tok::Symbol)
            return fail(ErrorCode::Directive, "#" + std::string(directive) + " needs a name",
                        t.line);
        return true;
    }

    bool path_arg(std::string_view directive, std::string& path) {
        Token t;
        if (!lex_.next(t, *err_)) return false;
        if (t.kind != Tok::Symbol && t.kind != Tok::String)
            return fail(ErrorCode::Directive, "#" + std::string(directive) + " needs a file path",
                        t.line);
        path.assign(t.text);
        return true;
    }

    bool conditional(const Token& t, bool parent_active) {
        const bool negated = t.text == "ifndef";
        bool value = false;
        if (parent_active) {
            Token name;
            if (!name_arg(t.text, name)) return false;
            value = s_.is_defined(Symbol(name.text)) != negated;
        }
        conds_.push_back({parent_active && value, parent_active, value, false, negated, t.line});
        return true;
    }

    bool else_or_endif(const Token& t) {
        if (conds_.empty())
            return fail(ErrorCode::Directive, "#" + std::string(t.text) + " without #ifdef",
                        t.line);
        if (t.text == "endif") {
            conds_.pop_back();
            return true;
        }
        Cond& c = conds_.back();
        if (c.seen_else) return fail(ErrorCode::Directive, "second #else for one #ifdef", t.line);
        c.seen_else = true;
        c.active = c.parent_active && !c.value;
        return true;
    }

    // Inside a branch that is not being read, only conditionals matter.
    bool skipped_directive(const Token& t) {
        if (t.text == "ifdef" || t.text == "ifndef") return conditional(t, false);
        if (t.text == "else" || t.text == "endif") return else_or_endif(t);
        return true;
    }

    bool directive(const Token& t) {
        const std::string_view d = t.text;
        if (d == "ifdef" || d == "ifndef") return conditional(t, true);
        if (d == "else" || d == "endif") return else_or_endif(t);
        if (d == "include" || d == "include_opt" || d == "merge") {
            std::string path;
            if (!path_arg(d, path)) return false;
            Ref<Array> root;
            if (!load(path, d == "include_opt", t.line, root)) return false;
            if (!root) return true;  // optional and missing
            if (d == "merge") {
                merge_missing_into(scratch_, frames_.back().start, *root);
                return true;
            }
            for (const Node& n : *root)
                if (!push(n)) return false;
            return true;
        }
        if (d == "define") {
            Token name;
            if (!name_arg(d, name)) return false;
            const Symbol sym(name.text);
            Token next;
            if (!lex_.peek(next, *err_)) return false;
            if (next.kind == Tok::Open && next.bracket == '(' && next.line == name.line) {
                (void)lex_.next(next, *err_);
                if (!open('(', next.line)) return false;
                frames_.back().kind = Frame::DefineBody;
                frames_.back().name = sym;
                return true;
            }
            s_.define(sym);
            return true;
        }
        if (d == "undef") {
            Token name;
            if (!name_arg(d, name)) return false;
            s_.undefine(Symbol(name.text));
            return true;
        }
        if (d == "autorun") {
            Token next;
            if (!lex_.next(next, *err_)) return false;
            if (next.kind != Tok::Open || next.bracket != '{')
                return fail(ErrorCode::Directive, "#autorun needs a {command}", next.line);
            if (!open('{', next.line)) return false;
            frames_.back().kind = Frame::Autorun;
            return true;
        }
        return fail(ErrorCode::Directive, "unknown directive '#" + std::string(d) + "'", t.line);
    }

    // Resolves and parses an included or merged file. Leaves `out` null if `optional`
    // and the file does not exist.
    bool load(const std::string& path, bool optional, int32_t line, Ref<Array>& out) {
        if (include_depth_ + 1 > s_.limits.max_include_depth)
            return fail(
                ErrorCode::Limit,
                "includes nested deeper than " + std::to_string(s_.limits.max_include_depth), line);
        if (s_.includes_ >= s_.limits.max_includes)
            return fail(ErrorCode::Limit,
                        "more than " + std::to_string(s_.limits.max_includes) + " included files",
                        line);

        namespace fs = std::filesystem;
        std::vector<std::string> candidates;
        const fs::path p(path);
        if (p.is_absolute()) {
            candidates.push_back(normalize_path(p));
        } else {
            candidates.push_back(normalize_path(fs::path(file_.str()).parent_path() / p));
            for (const std::string& dir : s_.include_dirs)
                candidates.push_back(normalize_path(fs::path(dir) / p));
        }
        std::string text;
        const std::string* found = nullptr;
        for (const std::string& c : candidates) {
            if (s_.reader_(c, text)) {
                found = &c;
                break;
            }
        }
        if (!found) {
            if (optional) return true;
            return fail(ErrorCode::Io, "cannot read included file '" + path + "'", line);
        }
        for (const std::string& open_file : s_.include_stack_)
            if (open_file == *found)
                return fail(ErrorCode::Directive, "'" + path + "' includes itself", line);

        ++s_.includes_;
        s_.include_stack_.push_back(*found);
        Parser sub(s_, text, Symbol(*found), include_depth_ + 1);
        const bool ok = sub.run(out, *err_);
        s_.include_stack_.pop_back();
        return ok;
    }

    LoadSession& s_;
    Lexer lex_;
    Symbol file_;
    uint32_t include_depth_;
    Error* err_ = nullptr;
    int32_t line_ = 1;
    std::vector<Node> scratch_;
    std::vector<FrameInfo> frames_;
    std::vector<Cond> conds_;
};

}  // namespace detail

LoadSession::LoadSession() : reader_(detail::read_disk_file) {}

void LoadSession::define(Symbol name) {
    defines_[name] = nullptr;
}
void LoadSession::define(Symbol name, Ref<Array> body) {
    defines_[name] = std::move(body);
}
void LoadSession::undefine(Symbol name) {
    defines_.erase(name);
}
bool LoadSession::is_defined(Symbol name) const {
    return defines_.count(name) != 0;
}
const Array* LoadSession::macro(Symbol name) const {
    auto it = defines_.find(name);
    return it == defines_.end() ? nullptr : it->second.get();
}
void LoadSession::set_reader(Reader reader) {
    reader_ = reader ? std::move(reader) : Reader(detail::read_disk_file);
}

Expected<Ref<Array>> try_read_string(std::string_view text, std::string_view name,
                                     LoadSession& session) {
    const bool top = session.include_stack_.empty();
    if (top) {
        session.nodes_ = 0;
        session.includes_ = 0;
    }
    const std::string file = detail::normalize_path(std::filesystem::path(name));
    session.include_stack_.push_back(file);
    struct Pop {
        std::vector<std::string>& stack;
        ~Pop() { stack.pop_back(); }
    } pop{session.include_stack_};

    detail::Parser parser(session, text, Symbol(file), 0);
    Ref<Array> root;
    Error err;
    if (!parser.run(root, err)) return Unexpected(std::move(err));
    return root;
}

Expected<Ref<Array>> try_read_string(std::string_view text, std::string_view name) {
    LoadSession session;
    return try_read_string(text, name, session);
}

Expected<Ref<Array>> try_read_file(std::string_view path, LoadSession& session) {
    const std::string file = detail::normalize_path(std::filesystem::path(path));
    std::string text;
    if (!session.reader_(file, text))
        return Unexpected(make_error(ErrorCode::Io, "cannot read '" + file + "'"));
    return try_read_string(text, file, session);
}

Expected<Ref<Array>> try_read_file(std::string_view path) {
    LoadSession session;
    return try_read_file(path, session);
}

#if MOIRA_EXCEPTIONS
namespace {
Ref<Array> value_or_raise(Expected<Ref<Array>> r) {
    if (!r) detail::raise(std::move(r).error());
    return std::move(*r);
}
}  // namespace

Ref<Array> read_file(std::string_view path, LoadSession& session) {
    return value_or_raise(try_read_file(path, session));
}
Ref<Array> read_file(std::string_view path) {
    return value_or_raise(try_read_file(path));
}
Ref<Array> read_string(std::string_view text, std::string_view name, LoadSession& session) {
    return value_or_raise(try_read_string(text, name, session));
}
Ref<Array> read_string(std::string_view text, std::string_view name) {
    return value_or_raise(try_read_string(text, name));
}
#endif

}  // namespace moira
