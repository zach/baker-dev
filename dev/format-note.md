# Laying Out Text as Baker Writes It, Keeping Its Comments

A design note for requirement note 0026 of the editor integration, agreed with the owner on
Oct 23, 2026. Results and deviations go into `dev/decisions.md` when it is built.

## The need

Nothing lays out Baker text. People align brackets by hand, an agent's edit leaves whatever
indentation it wrote, and each tool that writes a file writes as it can. A formatter in each
tool would be another copy of Baker's syntax and of its writer's rules, and would disagree
with what Baker writes.

## The design

- **What changes: whitespace only.** Every token, comment and directive is kept as written:
  number and string spellings, macros unexpanded, `#include` not followed. Only the gaps
  between them change. So the result reads as the same tree, with the same comments in the
  same order.
- **The layout is the writer's** (`write_dta`, `WriteOptions`), the owner's choice:
  - a form goes on one line if it fits the width at its indent;
  - otherwise its first item stays on the opening line, and each later item gets its own
    line, one indent step deeper;
  - the close follows the last item.

  A form holding a `;` comment or a directive is never put on one line. A `#remove` is a
  value, not a directive.
- **Directives keep their arguments** on their line: `#include path`, `#ifdef N`,
  `#define N`, and `#define N (` or `#autorun {` with the body opening on the same line,
  which the parser requires.
- **Comments.**
  - A `;` comment that followed code on its line stays there, as a trailing comment.
  - One on a line of its own stays on its own line, at the indent of what follows.
  - A block comment stays between the tokens it was between: on its own line if it was,
    else inline.
- **Alignment, as written** (the owner's choice).
  - A run of consecutive one-line sibling forms written aligned (each one's second item at
    the same column) stays aligned, re-padded to the run's widest first item.
  - A run of trailing comments on consecutive lines written at the same column stays
    aligned, one space past the run's longest line.
  - Text that was not aligned is never aligned.
- **Blank lines** (the owner's choice): a blank line written between two forms, at the top
  level or between children of a broken form, stays; a run of them becomes one. None is
  added, and blank lines at the start and the end go.
- **Text that does not parse** comes back unchanged, with the tolerant read's errors.
- **A range:** given a byte span, only the top-level forms it touches are laid out; the
  rest of the text is unchanged.
- **The edits, or the text.** Since only gaps change, each edit replaces one gap with a
  number of newlines and then a number of spaces: `{begin, end, newlines, spaces}`. An
  editor applies them without moving the cursor through unchanged text. The text is written
  to a sink as well.
- **A project's layout:** `(format (indent 3) (width 100))` in the project file
  (`Project::format`, a `WriteOptions`). The stacks' project files say so, since they are
  written with a three-space indent.
- **The kit's `format` command** (the owner's choice).
  - `mygame format FILE...` rewrites the files in place with the project's layout. With no
    files, it takes the files the stack reads under the project's directory.
  - `--baker-check` changes nothing and exits non-zero, listing each unformatted file, for
    CI and pre-commit hooks.
  - `--baker-json` gives each file's edits.
- **Idempotent:** formatting formatted text changes nothing.

## Where it lives

`baker::tools`: `tools::format_text(text, out, options)` beside `read_structure`, which it
reads with. No file is read.

## Cost

One tolerant read, one pass to lay out, one to render: linear in the text. It allocates
only the lists and the output. Target: a 2 MB file within 100 ms.

## Tests to write

- The writer's layout: a form that fits, one that does not, nested.
- Comments: trailing, own-line and block comments kept, and a form with a `;` comment
  broken.
- Directives: arguments kept on their line, `#define N (` together, `#remove` inline.
- Alignment: an aligned table re-aligned after a longer key is added; trailing comments
  re-aligned; unaligned text left unaligned.
- Blank lines: kept once, collapsed, trimmed at the ends.
- Text that does not parse: unchanged, with errors.
- A range: only the forms it touches are laid out.
- Edits: applying them to the old text gives the new text.
- Idempotent over the corpus, and each formatted file reads as the same tree as before.
- The project's `(format ...)`, and the `format` command with `--baker-check`.
- A 2 MB file's time.
