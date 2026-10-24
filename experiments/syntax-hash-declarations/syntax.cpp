#include "syntax.hpp"

#include "experiments.hpp"

#include <baker/config.hpp>
#include <baker/parse.hpp>
#include <baker/std_interop.hpp>
#include <baker/write.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {
namespace {

// --- A scanner with the lexer's token boundaries (src/data/parse.cpp, D220) ---

bool atom_end(unsigned char c) noexcept {
    return c <= 0x20 || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' ||
           c == '"' || c == ';';
}
bool is_letter(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool is_digit(unsigned char c) noexcept {
    return c >= '0' && c <= '9';
}
// Whether a bare atom could read as a number, so it cannot be an array's tag.
bool number_like(std::string_view a) noexcept {
    if (a.empty()) return false;
    if (is_digit(static_cast<unsigned char>(a[0]))) return true;
    if ((a[0] == '-' || a[0] == '+' || a[0] == '.') && a.size() > 1)
        return is_digit(static_cast<unsigned char>(a[1])) ||
               (a[1] == '.' && a.size() > 2 && is_digit(static_cast<unsigned char>(a[2])));
    return false;
}

struct Tok {
    enum Kind : uint8_t { End, Open, Close, Atom, String, Quoted };
    Kind kind = End;
    size_t begin = 0, end = 0;  // bytes; a string's or quoted symbol's include the quotes
    int32_t line = 1;
    char bracket = 0;
    [[nodiscard]] std::string_view text(std::string_view src) const {
        return src.substr(begin, end - begin);
    }
};

class Scanner {
public:
    explicit Scanner(std::string_view src) : src_(src) {
        if (src_.starts_with("\xEF\xBB\xBF")) pos_ = 3;
    }

    Tok next() {
        skip();
        Tok t;
        t.line = line_;
        t.begin = pos_;
        if (pos_ >= src_.size()) {
            t.end = pos_;
            return t;
        }
        const char c = src_[pos_];
        switch (c) {
            case '(':
            case '{':
            case '[':
                t.kind = Tok::Open;
                t.bracket = c;
                ++pos_;
                break;
            case ')':
            case '}':
            case ']':
                t.kind = Tok::Close;
                t.bracket = c;
                ++pos_;
                break;
            case '"':
            case '\'': {
                const size_t e = src_.find(c, pos_ + 1);
                pos_ = e == std::string_view::npos ? src_.size() : e + 1;
                t.kind = c == '"' ? Tok::String : Tok::Quoted;
                count_lines(t.begin, pos_);
                break;
            }
            default:
                while (pos_ < src_.size() && !atom_end(static_cast<unsigned char>(src_[pos_])))
                    ++pos_;
                t.kind = Tok::Atom;
        }
        t.end = pos_;
        return t;
    }

    Tok peek() {
        const size_t p = pos_;
        const int32_t l = line_;
        Tok t = next();
        pos_ = p;
        line_ = l;
        return t;
    }

private:
    void count_lines(size_t from, size_t to) {
        for (size_t i = from; i < to; ++i)
            if (src_[i] == '\n') ++line_;
    }
    void skip() {
        const size_t n = src_.size();
        while (pos_ < n) {
            const auto c = static_cast<unsigned char>(src_[pos_]);
            if (c <= 0x20) {
                if (c == '\n') ++line_;
                ++pos_;
            } else if (c == ';') {
                pos_ = std::min(src_.find('\n', pos_), n);
            } else if (c == '/' && pos_ + 1 < n && src_[pos_ + 1] == '*') {
                const size_t e = src_.find("*/", pos_ + 2);
                const size_t stop = e == std::string_view::npos ? n : e + 2;
                count_lines(pos_, stop);
                pos_ = stop;
            } else {
                break;
            }
        }
    }

    std::string_view src_;
    size_t pos_ = 0;
    int32_t line_ = 1;
};

// 1-based column in code points, as the parser gives it.
int32_t column_of(std::string_view src, size_t at) noexcept {
    size_t start = at == 0 ? std::string_view::npos : src.rfind('\n', at - 1);
    start = start == std::string_view::npos ? 0 : start + 1;
    int32_t col = 1;
    for (size_t i = start; i < at && i < src.size(); ++i)
        if ((static_cast<unsigned char>(src[i]) & 0xC0) != 0x80) ++col;
    return col;
}

Error located(std::string_view src, std::string_view file, const Tok& t, std::string_view msg,
              ErrorCode code = ErrorCode::Directive) {
    Error e = make_error(code, msg, Symbol(file), t.line);
    e.set_column(column_of(src, t.begin));
    return e;
}

std::string decimal(int64_t v) {
    return std::to_string(v);
}

// --- The reader's front half: new text -> the DTA Baker reads today ---

// The node the translation puts before a first form Baker would otherwise take for a
// declaration; read_with drops it.
constexpr std::string_view kDataFirst = "hash_declarations_data_first";

// `form`'s tokens on one line, comments dropped (a string keeps any newline it holds).
std::string one_line(std::string_view form) {
    Scanner sc(form);
    std::string out;
    for (Tok t = sc.next(); t.kind != Tok::End; t = sc.next()) {
        if (!out.empty() && t.kind != Tok::Close && out.back() != '(' && out.back() != '[' &&
            out.back() != '{')
            out += ' ';
        out += t.text(form);
    }
    return out;
}

class Translator {
public:
    Translator(std::string_view src, std::string_view name, std::vector<std::string>* notes,
               bool* data_first, bool refuse_shapes = false,
               std::vector<std::pair<int32_t, int32_t>>* hoisted_lines = nullptr)
        : src_(src),
          name_(name),
          notes_(notes),
          data_first_(data_first),
          refuse_shapes_(refuse_shapes),
          sc_(src) {
        hoisted_lines_ = hoisted_lines;
        out_.reserve(src.size() + 64);
    }

    Expected<std::string> run() {
        for (;;) {
            const Tok t = sc_.next();
            if (t.kind == Tok::End) break;
            bool ok = true;
            switch (t.kind) {
                case Tok::Open: ok = open(t); break;
                case Tok::Close: ok = close(t); break;
                case Tok::Atom: ok = atom(t); break;
                case Tok::String:
                case Tok::Quoted: ok = value(t); break;
                case Tok::End: break;
            }
            if (!ok) return Unexpected(std::move(err_));
        }
        for (const Frame& f : frames_)
            if (f.kind != Frame::Data)
                return Unexpected(located(src_, name_, f.at,
                                          std::string(f.directive()) + " ( from line " +
                                              decimal(f.at.line) + " is never closed"));
        close_shape_run();
        flush(src_.size());
        if (!hoisted_.empty()) {
            out_ += "\n(shapes";
            auto line = static_cast<int32_t>(1 + std::count(out_.begin(), out_.end(), '\n'));
            for (size_t i = 0; i < hoisted_.size(); ++i) {
                out_ += "\n";
                ++line;
                // Where it reads, and where it is written: read_with moves it back.
                if (hoisted_lines_) hoisted_lines_->emplace_back(line, hoisted_at_[i]);
                out_ += hoisted_[i];
                line +=
                    static_cast<int32_t>(std::count(hoisted_[i].begin(), hoisted_[i].end(), '\n'));
            }
            out_ += ")\n";
        }
        // A first form tagged pack, or origin with a string, is data in the new form; Baker
        // would take it for a declaration unless a node comes before it, so one does, and
        // read_with drops it (a declaration the file writes with # comes first either way).
        if ((first_pack_ && !pack_seen_) || (first_origin_ && !origin_seen_)) {
            out_.insert(first_out_, std::string(kDataFirst) + " ");
            if (data_first_) *data_first_ = true;
        }
        if (!prefix_.empty()) out_.insert(src_.starts_with("\xEF\xBB\xBF") ? 3 : 0, prefix_);
        return std::move(out_);
    }

private:
    struct Frame {
        enum Kind : uint8_t { Data, Pack, Origin, Shape, TableShape };
        Kind kind = Data;
        char open = '(';
        Tok at;                         // the directive, or the bracket
        bool tag_pending = true;        // the next value names the array
        std::string tag;                // its first token when that is a symbol
        bool moved = false;             // a declaration written after data: moved to the front
        bool shaped = false;            // Data: a #shape is written at this table
        size_t out_begin = 0;           // where in out_ the form starts
        size_t name_end = 0;            // TableShape: where in out_ the name ends
        std::vector<std::string> path;  // TableShape: the enclosing tags
        [[nodiscard]] std::string_view directive() const noexcept {
            switch (kind) {
                case Pack: return "#pack";
                case Origin: return "#origin";
                case Shape:
                case TableShape: return "#shape";
                case Data: break;
            }
            return "";
        }
    };

    bool fail(const Tok& t, std::string msg) {
        err_ = located(src_, name_, t, msg);
        return false;
    }
    void note(const Tok& t, std::string_view what) {
        if (notes_)
            notes_->push_back(std::string(name_) + ":" + decimal(t.line) + ": " +
                              std::string(what));
    }
    // Appends src_[copied_, upto).
    void flush(size_t upto) {
        if (upto > copied_) out_.append(src_, copied_, upto - copied_);
        copied_ = std::max(copied_, upto);
    }
    // Skips src_[copied_, upto), keeping its newlines so later lines keep their numbers.
    void drop(size_t upto) {
        for (size_t i = copied_; i < upto && i < src_.size(); ++i)
            if (src_[i] == '\n') out_ += '\n';
        copied_ = std::max(copied_, upto);
    }
    static void keep_newlines(std::string& out, std::string_view text) {
        for (const char c : text)
            if (c == '\n') out += '\n';
    }

    void close_shape_run() {
        if (!shape_run_) return;
        out_.insert(shape_run_close_, ")");
        shape_run_ = false;
    }
    // A token at the file's top level other than a `#shape`: the run, if open, ends, and
    // the token counts as a node when it `pushes` one (a declaration after one is moved).
    void top_level(bool pushes) {
        if (!frames_.empty()) return;
        close_shape_run();
        if (pushes) ++top_forms_;
    }

    bool open(const Tok& t) {
        // A top-level `[` defines a macro and #autorun's `{` is collected: neither pushes.
        const bool pushes = t.bracket != '[' && !autorun_;
        const bool first = frames_.empty() && pushes && top_forms_ == 0 && t.bracket == '(';
        autorun_ = false;
        top_level(pushes);
        if (first) {
            flush(t.begin);
            first_out_ = out_.size();
        }
        if (!frames_.empty()) {
            Frame& e = frames_.back();
            if (e.kind == Frame::TableShape && e.tag_pending)
                return fail(t, "#shape (NAME ...) needs a name");
            e.tag_pending = false;
        }
        Frame f;
        f.open = t.bracket;
        f.at = t;
        if (t.bracket != '(') f.tag_pending = false;
        frames_.push_back(std::move(f));
        return true;
    }

    bool close(const Tok& t) {
        if (frames_.empty()) return true;  // Baker reports the stray close
        Frame f = std::move(frames_.back());
        frames_.pop_back();
        switch (f.kind) {
            case Frame::Data: break;
            case Frame::Pack:
            case Frame::Origin: {
                flush(t.end);
                if (!f.moved) break;
                // Moved to the front on one line, so every line after keeps its number.
                std::string form = out_.substr(f.out_begin);
                out_.resize(f.out_begin);
                keep_newlines(out_, form);
                prefix_ += one_line(form) + " ";
                break;
            }
            case Frame::Shape:
                flush(t.end);
                shape_run_ = true;
                shape_run_close_ = out_.size();
                break;
            case Frame::TableShape: {
                flush(t.end);
                std::string form = out_.substr(f.out_begin);
                out_.resize(f.out_begin);
                keep_newlines(out_, form);
                const size_t split = f.name_end - f.out_begin;
                std::string h = form.substr(0, split) + " (at";
                for (const std::string& p : f.path) h += " " + p;
                h += ") (keyed)" + form.substr(split);
                hoisted_.push_back(std::move(h));
                hoisted_at_.push_back(f.at.line);
                break;
            }
        }
        return true;
    }

    bool value(const Tok& t) {
        top_level(true);
        if (frames_.empty()) return true;
        Frame& f = frames_.back();
        if (f.kind == Frame::Origin && f.tag_pending && t.kind != Tok::String)
            return fail(t, "#origin takes a string first: #origin (\"source\" (tool NAME))");
        if (f.kind == Frame::TableShape && f.tag_pending)
            return fail(t, "#shape (NAME ...) needs a name");
        if (f.tag_pending && t.kind == Tok::Quoted) f.tag = std::string(t.text(src_));
        f.tag_pending = false;
        return true;
    }

    bool atom(const Tok& t) {
        const std::string_view a = t.text(src_);
        if (a.size() >= 2 && a[0] == '#') {
            if (is_letter(static_cast<unsigned char>(a[1]))) return directive(t, a.substr(1));
            // `#1`, `#00aa00`, `#$%&`: symbols, which today's lexer needs quoted.
            top_level(true);
            if (a.find('\'') != std::string_view::npos)
                return fail(t, "the prototype cannot read '" + std::string(a) +
                                   "': a '#'-led symbol with a quote in it (the lexer rule reads "
                                   "it bare)");
            flush(t.begin);
            out_ += '\'';
            out_.append(a);
            out_ += '\'';
            copied_ = t.end;
            if (!frames_.empty()) frames_.back().tag_pending = false;
            return true;
        }
        top_level(true);
        if (frames_.empty()) return true;
        Frame& f = frames_.back();
        if (f.kind == Frame::Origin && f.tag_pending)
            return fail(t, "#origin takes a string first: #origin (\"source\" (tool NAME))");
        if (f.kind == Frame::TableShape && f.tag_pending) {
            if (number_like(a) || a[0] == '$') return fail(t, "#shape (NAME ...) needs a name");
            flush(t.end);
            f.name_end = out_.size();
            f.tag_pending = false;
            return true;
        }
        if (f.tag_pending && f.open == '(' && !number_like(a) && a[0] != '$') {
            f.tag = std::string(a);
            // `(at ...)` written inside an array's #shape would restate what the place says.
            if (frames_.size() >= 2 && frames_[frames_.size() - 2].kind == Frame::TableShape &&
                a == "at")
                return fail(f.at,
                            "(at ...) is not written inside an array's #shape: the shape governs "
                            "this array's children");
            if (frames_.size() == 1) legacy(f);
        }
        f.tag_pending = false;
        return true;
    }

    // The forms today's files use for what the `#` namespace now says: `check` would warn
    // once per layer. A first-form (pack ...) or (origin "...") is data here.
    void legacy(const Frame& f) {
        if (f.tag == "shapes")
            note(f.at, "a (shapes ...) container: write #shape (NAME ...) per shape");
        if (top_forms_ == 1 && f.tag == "pack") {
            first_pack_ = true;
            if (!pack_seen_) note(f.at, "a (pack ...) first form, read as data: write #pack (...)");
        }
        // Today an origin is declared first, or first after the pack declaration (D91).
        if ((top_forms_ == 1 || (top_forms_ == 2 && first_pack_ && !pack_seen_)) &&
            f.tag == "origin" && sc_.peek().kind == Tok::String) {
            first_origin_ = top_forms_ == 1;
            if (!origin_seen_)
                note(f.at, "an (origin \"...\") first form, read as data: write #origin (...)");
        }
    }

    // A directive's name or path argument, consumed so it is not taken for data.
    void argument(bool path) {
        const Tok a = sc_.peek();
        if (a.kind == Tok::Atom || (path && (a.kind == Tok::String || a.kind == Tok::Quoted)))
            (void)sc_.next();
    }

    bool directive(const Tok& t, std::string_view d) {
        if (d == "pack" || d == "origin") return declaration(t, d);
        if (d == "shape") return shape(t);
        if (d == "define") {
            top_level(false);
            const Tok name = sc_.peek();
            if (name.kind != Tok::Atom) return true;  // Baker reports it
            (void)sc_.next();
            const Tok next = sc_.peek();
            if (next.kind == Tok::Open && next.bracket == '(' && next.line == name.line)
                return fail(next, "#define " + std::string(name.text(src_)) +
                                      " defines a flag; a macro is written [" +
                                      std::string(name.text(src_)) + " ...]");
            return true;
        }
        // The rest are Baker's, as they are; their arguments are not data. A conditional is
        // tracked so nothing is moved out of one; an included file may push nodes, so a
        // declaration after it is moved to the front.
        if (d == "ifdef" || d == "ifndef") {
            argument(false);
            ++conds_;
        } else if (d == "endif") {
            conds_ = std::max(0, conds_ - 1);
        } else if (d == "undef") {
            argument(false);
        } else if (d == "include" || d == "include_opt" || d == "merge" || d == "require") {
            argument(true);
            if (frames_.empty()) {
                close_shape_run();
                ++includes_;
            } else {
                frames_.back().tag_pending = false;
            }
            return true;
        } else if (d == "autorun") {
            autorun_ = true;
        }
        top_level(d == "remove");
        if (!frames_.empty()) frames_.back().tag_pending = false;
        return true;
    }

    bool declaration(const Tok& t, std::string_view d) {
        const bool pack = d == "pack";
        if (!frames_.empty())
            return fail(t, "#" + std::string(d) + " is written at a file's top level");
        close_shape_run();
        if (pack ? pack_seen_ : origin_seen_)
            return fail(t, "second #" + std::string(d) + " in this file: a file declares one" +
                               (pack ? " pack" : " origin"));
        (pack ? pack_seen_ : origin_seen_) = true;
        const Tok paren = sc_.next();
        if (paren.kind != Tok::Open || paren.bracket != '(')
            return fail(paren, "#" + std::string(d) + " takes one form: #" + std::string(d) +
                                   (pack ? " (NAME (level L) ...)" : " (\"source\" (tool NAME))"));
        flush(t.begin);
        Frame f;
        f.kind = pack ? Frame::Pack : Frame::Origin;
        f.at = t;
        // Baker takes the declaration only before any node, and an included file may have
        // pushed some.
        f.moved = top_forms_ + includes_ > 0;
        if (f.moved && conds_ > 0)
            return fail(t, "the prototype cannot move a #" + std::string(d) +
                               " after data out of its #ifdef (adopted, the reader records "
                               "it where it stands)");
        f.out_begin = out_.size();
        out_ += pack ? "(pack " : "(origin ";
        drop(paren.end);
        frames_.push_back(std::move(f));
        return true;
    }

    bool shape(const Tok& t) {
        if (refuse_shapes_) {
            err_ = located(src_, name_, t,
                           "#shape declares a shape: a received layer read under data_only or "
                           "scalars_only may not",
                           ErrorCode::Refused);
            return false;
        }
        const Tok paren = sc_.next();
        if (paren.kind != Tok::Open || paren.bracket != '(')
            return fail(paren, "#shape takes one form: #shape (NAME (FIELD TYPE ...) ...)");
        Frame f;
        f.at = t;
        if (frames_.empty()) {
            flush(t.begin);
            f.kind = Frame::Shape;
            // The first of a run opens the container; the next ones join it, and its close
            // is written when the run ends.
            out_ += shape_run_ ? "(" : "(shapes (";
            shape_run_ = false;
            drop(paren.end);
            frames_.push_back(std::move(f));
            return true;
        }
        if (conds_ > 0)
            return fail(t,
                        "the prototype cannot hoist a #shape at its table out of its #ifdef "
                        "(adopted, the reader records it where it stands)");
        for (const Frame& e : frames_) {
            if (e.kind != Frame::Data || e.open != '(')
                return fail(t, "#shape is written at the top level or inside a (...) array");
            if (e.tag.empty())
                return fail(t,
                            "a #shape inside an array needs tagged ancestors: the array at "
                            "line " +
                                decimal(e.at.line) + " has no tag");
            f.path.push_back(e.tag);
        }
        if (frames_.back().shaped)
            return fail(t, "a second #shape at one table: the table's children have one shape");
        frames_.back().shaped = true;
        flush(t.begin);
        f.kind = Frame::TableShape;
        f.out_begin = out_.size();
        out_ += "(";
        drop(paren.end);
        frames_.back().tag_pending = false;
        frames_.push_back(std::move(f));
        return true;
    }

    std::string_view src_;
    std::string_view name_;
    std::vector<std::string>* notes_;
    bool* data_first_;
    bool refuse_shapes_;  // received text under a policy that refuses declarations
    Scanner sc_;
    std::string out_;
    size_t copied_ = 0;
    std::vector<Frame> frames_;
    std::vector<std::string> hoisted_;
    std::vector<int32_t> hoisted_at_;  // the line each hoisted #shape is written on
    std::vector<std::pair<int32_t, int32_t>>* hoisted_lines_ = nullptr;
    std::string prefix_;
    Error err_;
    int top_forms_ = 0;  // top-level nodes written so far: a declaration after one moves
    int includes_ = 0;   // top-level #include, #merge, #require: they may push nodes
    int conds_ = 0;      // open #ifdef/#ifndef
    bool pack_seen_ = false, origin_seen_ = false;
    bool autorun_ = false;                            // the next `{` is #autorun's
    bool first_pack_ = false, first_origin_ = false;  // the first form is (pack / (origin "
    size_t first_out_ = 0;                            // where in out_ the first form starts
    bool shape_run_ = false;  // a top-level (shapes ...) container awaits its close
    size_t shape_run_close_ = 0;
};

// --- The printer: tree -> the new form, with to_dta's layout (src/data/write.cpp) ---

constexpr int kIndent = 3;
constexpr int kWidth = 96;

bool is_shapes_container(const Node& n) {
    static const Symbol shapes("shapes");
    const Array* a = n.try_array();
    if (!a || a->size() < 2 || !(*a)[0].is(shapes)) return false;
    for (uint32_t i = 1; i < a->size(); ++i) {
        const Array* c = (*a)[i].try_array();
        if (!c || c->empty() || !(*c)[0].try_sym()) return false;
    }
    return true;
}

// A `#`-led symbol the new lexer reads bare: `#` alone, or before anything but a letter.
bool bare_hash_symbol(std::string_view s) noexcept {
    if (s.empty() || s[0] != '#') return false;
    if (s.size() >= 2 && is_letter(static_cast<unsigned char>(s[1]))) return false;
    for (const char c : s)
        if (atom_end(static_cast<unsigned char>(c)) || c == '\'') return false;
    return true;
}

char open_of(Type t) noexcept {
    return t == Type::Command ? '{' : t == Type::Property ? '[' : '(';
}
char close_of(Type t) noexcept {
    return t == Type::Command ? '}' : t == Type::Property ? ']' : ')';
}
bool marker_slot(const Node& n, const Array& a, uint32_t i) noexcept {
    return n.type() == Type::Array && a.size() == 2 && i == 1 && a[0].try_sym();
}

void put_atom(std::string& out, const Node& n, bool marker) {
    if (const auto s = n.try_sym()) {
        const std::string_view text = s->str();
        if ((marker && text == "#remove") || bare_hash_symbol(text)) {
            out += text;
            return;
        }
    }
    out += to_string(n);
}

bool put_flat(std::string& out, const Node& n, size_t limit, bool marker = false) {
    if (!n.is_array_like()) {
        put_atom(out, n, marker);
        return out.size() <= limit;
    }
    out += open_of(n.type());
    const Array& a = *n.array();
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) out += ' ';
        if (!put_flat(out, a[i], limit, marker_slot(n, a, i))) return false;
    }
    out += close_of(n.type());
    return out.size() <= limit;
}

// `prefix` is what the line already holds before `n` (the `#shape ` of a declaration).
void put_pretty(std::string& out, const Node& n, size_t indent, size_t prefix = 0,
                bool marker = false) {
    if (!n.is_array_like()) {
        put_atom(out, n, marker);
        return;
    }
    const size_t used = indent + prefix;
    const size_t budget = kWidth > used ? kWidth - used : 0;
    const size_t mark = out.size();
    if (put_flat(out, n, mark + budget, marker)) return;
    out.resize(mark);
    const Array& a = *n.array();
    out += open_of(n.type());
    for (uint32_t i = 0; i < a.size(); ++i) {
        if (i > 0) {
            out += '\n';
            out.append(indent + kIndent, ' ');
        }
        put_pretty(out, a[i], indent + kIndent, 0, marker_slot(n, a, i));
    }
    out += close_of(n.type());
}

// --- The migrator: today's DTA text -> the new form, keeping comments and layout ---

struct Edit {
    size_t begin, end;
    std::string text;
};

class Migrator {
public:
    explicit Migrator(std::string_view src) : src_(src), sc_(src) {}

    std::string run(Migration* report) {
        for (;;) {
            const Tok t = sc_.next();
            if (t.kind == Tok::End) break;
            switch (t.kind) {
                case Tok::Open: open(t); break;
                case Tok::Close: close(t); break;
                case Tok::Atom: atom(t); break;
                case Tok::String:
                case Tok::Quoted: value(t); break;
                case Tok::End: break;
            }
        }
        std::string out;
        out.reserve(src_.size() + 64);
        size_t at = 0;
        for (const Edit& e : edits_) {
            if (e.begin < at) continue;  // never: edits are made in order
            out.append(src_, at, e.begin - at);
            out += e.text;
            at = e.end;
        }
        out.append(src_, at, std::string::npos);
        if (report) *report = m_;
        return out;
    }

private:
    struct Frame {
        enum Kind : uint8_t { Data, Shapes, DefineBody };
        Kind kind = Data;
        char open = '(';
        Tok at;
        bool tag_pending = true;
        std::string tag;
        size_t tag_end = 0;
        std::vector<std::pair<size_t, size_t>> children;  // Shapes: each shape's span
        bool plain = true;                                // Shapes: every child is (NAME ...)
    };

    void open(const Tok& t) {
        if (!frames_.empty()) frames_.back().tag_pending = false;
        // A top-level `[` defines a macro and #autorun's `{` is collected: neither pushes.
        if (frames_.empty() && t.bracket != '[' && !autorun_) ++top_forms_;
        autorun_ = false;
        Frame f;
        f.open = t.bracket;
        f.at = t;
        if (t.bracket != '(') f.tag_pending = false;
        frames_.push_back(std::move(f));
    }

    void value(const Tok& t) {
        if (frames_.empty()) {
            ++top_forms_;
            return;
        }
        Frame& f = frames_.back();
        if (!f.tag_pending) {
            if (f.kind == Frame::Shapes) f.plain = false;
            return;
        }
        if (t.kind == Tok::Quoted) f.tag = std::string(t.text(src_));
        f.tag_pending = false;
    }

    void atom(const Tok& t) {
        const std::string_view a = t.text(src_);
        if (a.size() >= 2 && a[0] == '#' && a.substr(1) == "define") {
            const Tok name = sc_.peek();
            if (name.kind != Tok::Atom) return;
            (void)sc_.next();
            const Tok next = sc_.peek();
            if (next.kind == Tok::Open && next.bracket == '(' && next.line == name.line &&
                frames_.empty()) {
                (void)sc_.next();
                edits_.push_back({t.begin, next.end, "[" + std::string(name.text(src_)) + " "});
                Frame f;
                f.kind = Frame::DefineBody;
                f.at = next;
                f.tag_pending = false;
                frames_.push_back(std::move(f));
                ++m_.define_bodies;
            }
            return;
        }
        if (a.size() >= 2 && a[0] == '#') {
            // Today's directives. Their arguments are not data; an included file may push
            // nodes, so a (pack ...) after one is left as it is (Baker reads it as data then).
            const std::string_view d = a.substr(1);
            const Tok arg = sc_.peek();
            if ((d == "ifdef" || d == "ifndef" || d == "undef") && arg.kind == Tok::Atom) {
                (void)sc_.next();
            } else if (d == "include" || d == "include_opt" || d == "merge" || d == "require") {
                if (arg.kind == Tok::Atom || arg.kind == Tok::String || arg.kind == Tok::Quoted)
                    (void)sc_.next();
                if (frames_.empty()) ++top_forms_;
            } else if (d == "autorun") {
                autorun_ = true;
            }
            if (!frames_.empty()) frames_.back().tag_pending = false;
            return;
        }
        if (frames_.empty()) {
            ++top_forms_;
            return;
        }
        Frame& f = frames_.back();
        if (!f.tag_pending) {
            if (f.kind == Frame::Shapes) f.plain = false;
            return;
        }
        f.tag_pending = false;
        if (f.open != '(' || number_like(a) || a[0] == '$') return;
        f.tag = std::string(a);
        f.tag_end = t.end;
        if (frames_.size() != 1) return;
        // The file's first form (nothing pushed before it) may be a declaration.
        if (a == "pack" && !pack_done_ && top_forms_ == 1) {
            const Tok name = sc_.peek();
            if (name.kind == Tok::Atom || name.kind == Tok::Quoted) {
                edits_.push_back({f.at.begin, name.begin, "#pack ("});
                pack_done_ = true;
                --top_forms_;
                ++m_.packs;
            }
        } else if (a == "origin" && !origin_done_ && top_forms_ == 1) {
            const Tok source = sc_.peek();
            if (source.kind == Tok::String) {
                edits_.push_back({f.at.begin, source.begin, "#origin ("});
                origin_done_ = true;
                --top_forms_;
                ++m_.origins;
            }
        } else if (a == "shapes") {
            f.kind = Frame::Shapes;
        }
    }

    void close(const Tok& t) {
        if (frames_.empty()) return;
        Frame f = std::move(frames_.back());
        frames_.pop_back();
        if (f.kind == Frame::DefineBody) {
            edits_.push_back({t.begin, t.end, "]"});
            return;
        }
        if (!frames_.empty() && frames_.back().kind == Frame::Shapes) {
            Frame& s = frames_.back();
            // A shape is (NAME ...): an array whose first token is a symbol.
            if (f.open != '(' || f.tag.empty()) s.plain = false;
            s.children.emplace_back(f.at.begin, t.end);
            return;
        }
        if (f.kind == Frame::Shapes) split(f, t);
    }

    // The container's children, each prefixed `#shape `, one indent level out.
    void split(const Frame& f, const Tok& close) {
        if (f.children.empty() || !f.plain) return;
        const std::string unit = indent_before(f.children[0].first);
        std::string text;
        size_t prev = f.tag_end;
        for (size_t i = 0; i < f.children.size(); ++i) {
            const auto [cb, ce] = f.children[i];
            const std::string_view gap = src_.substr(prev, cb - prev);
            if (i == 0) {
                // The rest of the `(shapes` line goes; what follows begins a line.
                const size_t nl = gap.find('\n');
                if (nl != std::string_view::npos) dedent(text, gap.substr(nl + 1), unit, true);
            } else if (gap.find('\n') == std::string_view::npos) {
                text += '\n';
            } else {
                dedent(text, gap, unit);
            }
            text += "#shape ";
            dedent(text, src_.substr(cb, ce - cb), unit);
            prev = ce;
        }
        dedent(text, src_.substr(prev, close.begin - prev), unit);
        edits_.push_back({f.at.begin, close.end, std::move(text)});
        ++m_.shape_sets;
        m_.shapes += static_cast<int>(f.children.size());
    }

    // The whitespace that opens the line `at` is on, when it is the line's first text.
    [[nodiscard]] std::string indent_before(size_t at) const {
        size_t start = at == 0 ? std::string_view::npos : src_.rfind('\n', at - 1);
        start = start == std::string_view::npos ? 0 : start + 1;
        for (size_t i = start; i < at; ++i)
            if (src_[i] != ' ' && src_[i] != '\t') return {};
        return std::string(src_.substr(start, at - start));
    }

    // Appends `text`, removing `unit` (or what there is of it) after each newline, and at
    // the start when `text` begins a line.
    static void dedent(std::string& out, std::string_view text, const std::string& unit,
                       bool line_start = false) {
        size_t i = 0;
        if (line_start)
            while (i < unit.size() && i < text.size() && text[i] == unit[i]) ++i;
        while (i < text.size()) {
            const size_t nl = text.find('\n', i);
            const size_t stop = nl == std::string_view::npos ? text.size() : nl + 1;
            out.append(text, i, stop - i);
            i = stop;
            if (nl == std::string_view::npos) break;
            size_t k = 0;
            while (k < unit.size() && i + k < text.size() && text[i + k] == unit[k]) ++k;
            i += k;
        }
    }

    std::string_view src_;
    Scanner sc_;
    std::vector<Frame> frames_;
    std::vector<Edit> edits_;
    Migration m_;
    int top_forms_ = 0;
    bool autorun_ = false;  // the next `{` is #autorun's
    bool pack_done_ = false, origin_done_ = false;
};

}  // namespace

Expected<std::string> translate(std::string_view text, std::string_view name,
                                std::vector<std::string>* notes, bool* data_first) {
    if (data_first) *data_first = false;
    Translator t(text, name, notes, data_first);
    return t.run();
}

namespace {

// `a` and every array in it, `delta` lines later.
Ref<Array> shifted(const Array& a, int32_t delta) {
    std::vector<Node> nodes;
    nodes.reserve(a.size());
    for (const Node& n : a) {
        if (const Array* c = n.array())
            nodes.emplace_back(shifted(*c, delta), n.type());
        else
            nodes.push_back(n);
    }
    return Array::make_from(nodes, a.file(), a.line() + delta);
}

}  // namespace

Expected<Ref<Array>> read_with(std::string_view text, std::string_view name, LoadSession& session,
                               std::vector<std::string>* notes) {
    bool data_first = false;
    std::vector<std::pair<int32_t, int32_t>> hoisted;
    Translator t(text, name, notes, &data_first, false, &hoisted);
    auto dta = t.run();
    if (!dta) return Unexpected(dta.error());
    auto root = read_string(*dta, name, session);
    if (!root || (!data_first && hoisted.empty())) return root;
    const Array& r = **root;
    std::vector<Node> rest;
    rest.reserve(r.size());
    bool dropped = !data_first;
    for (const Node& n : r) {
        // The node put before a first form that is data goes (after any an #include spliced).
        if (!dropped && n.try_sym() && n.try_sym()->str() == kDataFirst) {
            dropped = true;
            continue;
        }
        rest.push_back(n);
    }
    // The shapes written at their tables, read from the container at the end, go back to the
    // lines they are written on, so a field's file and line are its declaration's.
    if (!hoisted.empty() && !rest.empty()) {
        if (const Array* c = rest.back().array(); c && c->size() == hoisted.size() + 1) {
            std::vector<Node> shapes{(*c)[0]};
            for (size_t i = 0; i < hoisted.size(); ++i) {
                const Array* h = (*c)[static_cast<uint32_t>(i + 1)].array();
                if (!h) return root;
                shapes.emplace_back(shifted(*h, hoisted[i].second - hoisted[i].first));
            }
            rest.back() = Node(Array::make_from(shapes, c->file(), c->line()));
        }
    }
    return Array::make_from(rest, r.file(), r.line());
}

namespace {

// An included file in the new form, for a session's reader.
bool serve(std::string_view path, std::string_view text, TextSink out) {
    bool data_first = false;
    auto dta = translate(text, path, nullptr, &data_first);
    if (!dta) {
        std::fprintf(stderr, "%s\n", to_string(dta.error()).data());
        return false;
    }
    if (data_first) {
        // The node that keeps it data would be spliced into the including file.
        std::fprintf(stderr,
                     "%.*s: the prototype cannot include a file whose first form is a (pack "
                     "...) or (origin \"...\") read as data\n",
                     static_cast<int>(path.size()), path.data());
        return false;
    }
    out(*dta);
    return true;
}

}  // namespace

Expected<LayerReport> check_received(const Config& cfg, std::string_view name,
                                     std::string_view text, const LayerPolicy& policy) {
    Translator t(text, name, nullptr, nullptr, policy.data_only || policy.scalars_only);
    auto dta = t.run();
    if (!dta) return Unexpected(dta.error());
    return cfg.check_layer_text(name, *dta, policy);
}

LoadSession corpus_session() {
    LoadSession session;
    session.set_pack_levels({"engine", "game", "task", "live"});
    // Files the text includes may be in the new form too.
    session.set_reader([](std::string_view path, TextSink out) {
        auto text = read_text_file(path);
        return text && serve(path, *text, out);
    });
    return session;
}

Expected<Ref<Array>> read(std::string_view text, std::string_view name) {
    LoadSession session = corpus_session();
    return read_with(text, name, session);
}

std::string print(const Array& root) {
    std::string out;
    for (const Node& n : root) {
        if (is_shapes_container(n)) {
            const Array& a = *n.array();
            for (uint32_t i = 1; i < a.size(); ++i) {
                out += "#shape ";
                put_pretty(out, a[i], 0, 7);
                out += '\n';
            }
            continue;
        }
        put_pretty(out, n, 0);
        out += '\n';
    }
    if (out.starts_with("\xEF\xBB\xBF")) out.insert(0, " ");
    return out;
}

std::string migrate(std::string_view dta, Migration* report) {
    Migrator m(dta);
    return m.run(report);
}

void serve_migrated(LoadSession& session, std::vector<Migration>* reports) {
    session.set_reader([reports](std::string_view path, TextSink out) {
        auto text = read_text_file(path);
        if (!text) return false;
        Migration m;
        const std::string migrated = migrate(*text, &m);
        if (reports) reports->push_back(m);
        return serve(path, migrated, out);
    });
}

}  // namespace baker::experiments::syntax
