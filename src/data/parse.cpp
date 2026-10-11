#include <moira/parse.hpp>

#include "merge.hpp"
#include "text.hpp"

#include <moira/std_interop.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace moira {
namespace detail {
namespace {

enum class Tok : uint8_t { End, Open, Close, Int, Float, String, Symbol, Var, Directive };

struct Token {
    Tok kind = Tok::End;
    char bracket = 0;     // Open, Close
    bool quoted = false;  // Symbol written as 'text'
    int32_t line = 0;
    uint32_t begin = 0, end = 0;  // byte span in the source
    int64_t i = 0;
    double f = 0;
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
        const bool ok = lex_token(t, err);
        t.end = static_cast<uint32_t>(pos_);
        return ok;
    }

    bool lex_token(Token& t, Error& err) {
        t = Token{};
        const size_t n = src_.size();
        for (;;) {
            if (pos_ >= n) {
                t.kind = Tok::End;
                t.line = line_;
                t.begin = static_cast<uint32_t>(pos_);
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
        t.begin = static_cast<uint32_t>(pos_);
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
                            "integer " + std::string(atom) + " is out of 64-bit range", t.line);
            case NumberKind::FloatOutOfRange:
                return fail(err, ErrorCode::Range,
                            "float " + std::string(atom) + " is out of double range", t.line);
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

bool read_disk_file(std::string_view path, TextOut out) {
    std::FILE* f = std::fopen(std::string(path).c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) out(std::string_view(buf, got));
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

}  // namespace

// Reads `path` through `reader` into `text`, replacing what it held.
bool read_with(const LoadSession::Reader& reader, std::string_view path, std::string& text) {
    text.clear();
    return reader(path, string_out(text));
}

}  // namespace detail

struct LoadSession::State {
    std::unordered_map<Symbol, Ref<Array>> defines;
    Reader reader = detail::read_disk_file;
    std::vector<std::string> include_dirs;
    std::vector<Node> autorun;
    // Per top-level read.
    std::vector<std::string> include_stack;
    uint64_t nodes = 0;
    uint32_t includes = 0;
};

namespace detail {

class Parser {
public:
    Parser(LoadSession& session, std::string_view text, Symbol file, uint32_t include_depth)
        : s_(session),
          lex_(text, file),
          file_(file),
          include_depth_(include_depth),
          map_(session.source_map),
          size_(static_cast<uint32_t>(text.size())) {
        if (map_) map_->record_text(file, text);
    }

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
                case Tok::Open: ok = open(t.bracket, t.line, t.begin); break;
                case Tok::Close: ok = close(t); break;
                case Tok::Int: ok = push(Node(t.i), here(t)); break;
                case Tok::Float: ok = push(Node(t.f), here(t)); break;
                case Tok::String: ok = push(Node(String::make(t.text)), here(t)); break;
                case Tok::Symbol: ok = symbol(t); break;
                case Tok::Var: ok = push(Node::var(Symbol(t.text)), here(t)); break;
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
        if (map_) map_->record(root, {file_, 0, size_, {}}, spans_);
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
        uint32_t begin = 0;  // offset of the opening bracket
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

    [[nodiscard]] SourceSpan here(const Token& t) const noexcept {
        return {file_, t.begin, t.end, {}};
    }

    // Edit mode keeps spans_ parallel to scratch_.
    bool push(Node n, const SourceSpan& span = {}) {
        if (++s_.st_->nodes > s_.limits.max_nodes)
            return fail(ErrorCode::Limit,
                        "more than " + std::to_string(s_.limits.max_nodes) + " nodes", line_);
        if (frames_.back().kind == Frame::Root && n.type() == Type::Property)
            return fail(ErrorCode::Syntax,
                        "a [property] cannot appear at top level, where '[' defines a macro",
                        line_);
        scratch_.push_back(std::move(n));
        if (map_) spans_.push_back(span);
        return true;
    }

    void truncate(size_t size) {
        scratch_.resize(size);
        if (map_) spans_.resize(size);
    }
    // Records `arr` (made from the nodes from `start`) and its span, for edit mode.
    void record(const Ref<Array>& arr, size_t start, uint32_t begin, uint32_t end) {
        if (!map_) return;
        map_->record(arr, {file_, begin, end, {}},
                     std::span<const SourceSpan>(spans_).subspan(start));
    }

    bool symbol(const Token& t) {
        const Symbol sym(t.text);
        const FrameInfo& f = frames_.back();
        const bool naming_macro = f.kind == Frame::MacroDef && scratch_.size() == f.start;
        if (!t.quoted && !naming_macro && !s_.st_->defines.empty()) {
            if (const Array* body = s_.macro(sym)) {
                const SourceSpan from{file_, t.begin, t.end, sym};
                for (const Node& n : *body)
                    if (!push(n, from)) return false;
                return true;
            }
        }
        return push(Node(sym), here(t));
    }

    bool open(char bracket, int32_t line, uint32_t begin) {
        if (frames_.size() > s_.limits.max_depth) return too_deep(line);
        Frame kind = Frame::Array;
        if (bracket == '{') kind = Frame::Command;
        if (bracket == '[')
            kind = frames_.back().kind == Frame::Root ? Frame::MacroDef : Frame::Property;
        frames_.push_back({kind, scratch_.size(), line, {}, bracket, begin});
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
            record(body, f.start + 1, f.begin, t.end);
            truncate(f.start);
            if (body->height() > s_.limits.max_depth) return too_deep(f.line);
            s_.define(*name, std::move(body));
            return true;
        }

        Ref<Array> arr = Array::make_from(kids, file_, f.line);
        record(arr, f.start, f.begin, t.end);
        truncate(f.start);
        if (arr->height() > s_.limits.max_depth) return too_deep(f.line);
        const SourceSpan span{file_, f.begin, t.end, {}};
        switch (f.kind) {
            case Frame::DefineBody: s_.define(f.name, std::move(arr)); return true;
            case Frame::Autorun:
                s_.st_->autorun.emplace_back(std::move(arr), Type::Command);
                return true;
            case Frame::Command: return push(Node(std::move(arr), Type::Command), span);
            case Frame::Property: return push(Node(std::move(arr), Type::Property), span);
            default: return push(Node(std::move(arr)), span);
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
                merge_into(scratch_, map_ ? &spans_ : nullptr, frames_.back().start, *root, 0,
                           MergePolicy::FillMissing, map_);
                return true;
            }
            for (size_t i = 0; i < root->size(); ++i) {
                const SourceSpan* sp = map_ ? map_->span(*root, i) : nullptr;
                if (!push((*root)[i], sp ? *sp : SourceSpan{})) return false;
            }
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
                if (!open('(', next.line, next.begin)) return false;
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
            if (!open('{', next.line, next.begin)) return false;
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
        if (s_.st_->includes >= s_.limits.max_includes)
            return fail(ErrorCode::Limit,
                        "more than " + std::to_string(s_.limits.max_includes) + " included files",
                        line);

        std::vector<std::string> candidates;
        if (is_absolute_path(path)) {
            candidates.push_back(normalize_path(path));
        } else {
            candidates.push_back(normalize_path(join_path(parent_path(file_.str()), path)));
            for (const std::string& dir : s_.st_->include_dirs)
                candidates.push_back(normalize_path(join_path(dir, path)));
        }
        std::string text;
        const std::string* found = nullptr;
        for (const std::string& c : candidates) {
            if (read_with(s_.st_->reader, c, text)) {
                found = &c;
                break;
            }
        }
        if (!found) {
            if (optional) return true;
            return fail(ErrorCode::Io, "cannot read included file '" + path + "'", line);
        }
        for (const std::string& open_file : s_.st_->include_stack)
            if (open_file == *found)
                return fail(ErrorCode::Directive, "'" + path + "' includes itself", line);

        ++s_.st_->includes;
        s_.st_->include_stack.push_back(*found);
        Parser sub(s_, text, Symbol(*found), include_depth_ + 1);
        const bool ok = sub.run(out, *err_);
        s_.st_->include_stack.pop_back();
        return ok;
    }

    LoadSession& s_;
    Lexer lex_;
    Symbol file_;
    uint32_t include_depth_;
    Error* err_ = nullptr;
    int32_t line_ = 1;
    SourceMap* map_;
    uint32_t size_;
    std::vector<Node> scratch_;
    std::vector<SourceSpan> spans_;  // parallel to scratch_ in edit mode
    std::vector<FrameInfo> frames_;
    std::vector<Cond> conds_;
};

}  // namespace detail

// Sessions belong to whoever made them: from the default allocator (D61).
LoadSession::LoadSession() : st_(detail::create<State>(default_allocator())) {}
LoadSession::LoadSession(const LoadSession& o)
    : limits(o.limits),
      source_map(o.source_map),
      st_(detail::create<State>(default_allocator(), *o.st_)) {}
LoadSession::LoadSession(LoadSession&& o) noexcept
    : limits(o.limits), source_map(o.source_map), st_(o.st_) {
    o.st_ = detail::create<State>(
        default_allocator());  // the moved-from session stays usable, and empty
}
LoadSession& LoadSession::operator=(const LoadSession& o) {
    if (this != &o) {
        State* st = detail::create<State>(default_allocator(), *o.st_);
        detail::destroy(default_allocator(), st_);
        st_ = st;
        limits = o.limits;
        source_map = o.source_map;
    }
    return *this;
}
LoadSession& LoadSession::operator=(LoadSession&& o) noexcept {
    std::swap(st_, o.st_);
    limits = o.limits;
    source_map = o.source_map;
    return *this;
}
LoadSession::~LoadSession() {
    detail::destroy(default_allocator(), st_);
}

void LoadSession::define(Symbol name) {
    st_->defines[name] = nullptr;
}
void LoadSession::define(Symbol name, Ref<Array> body) {
    st_->defines[name] = std::move(body);
}
void LoadSession::undefine(Symbol name) {
    st_->defines.erase(name);
}
bool LoadSession::is_defined(Symbol name) const {
    return st_->defines.count(name) != 0;
}
const Array* LoadSession::macro(Symbol name) const {
    auto it = st_->defines.find(name);
    return it == st_->defines.end() ? nullptr : it->second.get();
}
void LoadSession::set_reader(Reader reader) {
    st_->reader = reader ? std::move(reader) : Reader(detail::read_disk_file);
}
const LoadSession::Reader& LoadSession::reader() const noexcept {
    return st_->reader;
}
void LoadSession::add_include_dir(std::string_view dir) {
    st_->include_dirs.emplace_back(dir);
}
void LoadSession::clear_include_dirs() {
    st_->include_dirs.clear();
}
std::span<const Node> LoadSession::autorun() const noexcept {
    return st_->autorun;
}
void LoadSession::clear_autorun() {
    st_->autorun.clear();
}

Expected<Ref<Array>> read_string(std::string_view text, std::string_view name,
                                 LoadSession& session) {
    const bool top = session.st_->include_stack.empty();
    if (top) {
        session.st_->nodes = 0;
        session.st_->includes = 0;
    }
    const std::string file = detail::normalize_path(name);
    session.st_->include_stack.push_back(file);
    struct Pop {
        std::vector<std::string>& stack;
        ~Pop() { stack.pop_back(); }
    } pop{session.st_->include_stack};

    detail::Parser parser(session, text, Symbol(file), 0);
    Ref<Array> root;
    Error err;
    if (!parser.run(root, err)) return Unexpected(std::move(err));
    return root;
}

Expected<Ref<Array>> read_string(std::string_view text, std::string_view name) {
    LoadSession session;
    return read_string(text, name, session);
}

Expected<Ref<Array>> read_file(std::string_view path, LoadSession& session) {
    const std::string file = detail::normalize_path(path);
    std::string text;
    if (!detail::read_with(session.st_->reader, file, text))
        return Unexpected(make_error(ErrorCode::Io, "cannot read '" + file + "'"));
    return read_string(text, file, session);
}

Expected<Ref<Array>> read_file(std::string_view path) {
    LoadSession session;
    return read_file(path, session);
}

}  // namespace moira
