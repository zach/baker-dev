#pragma once

// Curly-infix (dev/syntax-curly-infix-design.md): a {...} whose odd slots hold one operator
// reads head-first, `{0.3 <= $value <= 0.7}` for `{<= 0.3 $value 0.7}`, and `{c ? a : b}` is
// `{if c a b}`. The rule is in the core's reader and printer on this branch, so this
// experiment's reader is baker::read_string. What lives here: a printer that writes every
// command it can as infix (the strongest round trip), the prefix printer (today's DTA), and
// the converter that rewrites a DTA file's operator calls in place, keeping its comments and
// layout, with the counts the design note reports. A tool: the STL is used freely.

#include <baker/array.hpp>
#include <baker/expected.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baker::experiments::syntax {

constexpr std::string_view kName = "curly-infix";
constexpr std::string_view kExtension = ".dta";  // the same files: the reader takes both forms

// Reads either form: baker::read_string, whose reader applies the infix rule.
[[nodiscard]] Expected<Ref<Array>> read(std::string_view text, std::string_view name);

// Every command whose shape allows it marked written infix, then printed: a chain for an
// operator with two or more operands and an operand that fires the guard, a ternary for an
// `if` of two or three arguments. The tree is copied, never changed.
[[nodiscard]] std::string print(const Array& root);

// Today's DTA: every command printed prefix (the written-infix marks cleared on a copy).
[[nodiscard]] std::string print_prefix(const Array& root);

// Brackets, atoms and characters of a text (strings one atom, comments skipped; characters
// of its compact form: one space between tokens, none inside a bracket's edge).
struct TextSize {
    int brackets = 0, atoms = 0, chars = 0;
    void add(const TextSize& o) {
        brackets += o.brackets;
        atoms += o.atoms;
        chars += o.chars;
    }
};
[[nodiscard]] TextSize text_size(std::string_view text);

// What the converter did to one file, and what it cost or saved.
struct Counts {
    int top_commands = 0;      // commands written in the file and inside no other command
    int top_changed = 0;       // of those, rewritten
    int chains = 0;            // {op a b ...} written {a op b ...}
    int bands = 0;             // {and {>= $v lo} {<= $v hi}} as {lo <= $v <= hi}
    int ascending = 0;         // {> $v 0} beside a bound of the same $v, turned to {0 < $v}
    int ternaries = 0;         // {if c a b} on one line as {c ? a : b}
    int assignments = 0;       // {set $x {+ $x n}} as {$x += n}
    int flattened = 0;         // {+ {+ a b} c} as {a + b + c}: a nest of one operator merged
    int reassociated = 0;      // of those, a nest not leftmost ({+ a {+ b c}}): exact for ints
    int kept_for_comment = 0;  // left prefix: a comment between operands would be lost
    // The top-level commands' text before and after: the changed ones, and all of them.
    TextSize changed_before, changed_after, all_before, all_after;
    void add(const Counts& o);
};

// A rewrite whose meaning rests on the vocabulary, not only on the tree being the same: a
// band, a turned comparison, an assignment, a reassociated sum. `check` evaluates both.
struct Rewrite {
    enum class Kind { Band, Ascending, Assignment, Flatten } kind;
    Node before;  // the command as written
    Node after;   // the command it became
};

// One changed top-level command, for the note's tables.
struct Example {
    int line = 0;
    std::string before, after;
};

struct Converted {
    std::string text;  // the file with its operator calls rewritten
    Ref<Array> tree;   // what `text` must read as: written-infix marks included
    Counts counts;
    std::vector<Rewrite> rewrites;
    std::vector<Example> examples;
};

// Reads the DTA file at `path` in edit mode and rewrites its operator calls as infix, in
// place: each command's bytes are replaced, every other byte of the file is kept.
[[nodiscard]] Expected<Converted> to_new(std::string_view path);
// The same over `text` read as the file at `path`, its includes from beside it: an earlier
// version of the file, such as its original before the in-tree rewrite.
[[nodiscard]] Expected<Converted> to_new(std::string_view path, std::string_view text);

// The files the in-tree rewrite changed (dev/syntax-curly-infix-design.md, section 7). Each
// original is kept as samples/<name>.original.dta, git's copy from before the rewrite, so a
// checkout without history can still prove the rewrite.
struct Original {
    std::string_view name;  // samples/<name>.original.dta
    std::string_view path;  // the rewritten file, from the repository root
};
[[nodiscard]] std::span<const Original> originals();
// The text a corpus file had before the rewrite: its original's, or its own.
[[nodiscard]] Expected<std::string> original_text(std::string_view rel);
// How many commands of `root` written in `file` (not in what it includes) read as infix.
[[nodiscard]] int written_infix(const Array& root, std::string_view file);

// Evaluates a rewrite's `before` and `after` with each variable they read set to values
// around each number they hold. Returns the first disagreement, or empty; `evaluated` is
// false when neither can run here (they call host functions a bare Context lacks).
[[nodiscard]] std::string agree(const Rewrite& w, bool& evaluated);

// What proves a rewritten file against its original's text, read as the file at `path`:
// the original has no brace the infix rule rewrote (so it reads as with the switch off); the
// file reads to the converter's tree of the original, written-infix marks included; that
// tree, with each rewrite resting on the vocabulary (a band, a turned comparison, an
// assignment, a merged nest) undone, is the original's tree exactly, and each such rewrite
// evaluates as what it replaced; and the file keeps the original's comments, each on its
// line, and its line count. Empty if all hold, else the first that does not; `summary` says
// what was compared.
[[nodiscard]] std::string prove_rewrite(std::string_view path, std::string_view original,
                                        std::string* summary = nullptr);

// Whether two trees carry the written-infix mark on the same commands; empty if they do,
// else where they first differ.
[[nodiscard]] std::string first_mark_difference(const Array& a, const Array& b);

}  // namespace baker::experiments::syntax
