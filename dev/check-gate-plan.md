# The Check Gate and the Stacks' Commands for Tools

A design note for five requirement notes from the agent-bridge integration (`baker-mcp`,
`requirements/0001`–`0005`). Their text was brought here by the owner, as
`INTEGRATIONS.md` asks. Each is designed below as Baker's own feature, or as the stacks'
own feature, for any host or tool that gates and drives a game from outside: an agent, a
pre-commit hook, CI, an editor.

Oct 16, 2026. The owner chose to take up all five. Results go into `dev/decisions.md`.

---

## 0. What each note asks, and where it lands

| Note | The need | Where |
|---|---|---|
| 0005 | `check` fails when the data does not load cleanly; sees into script that `#autorun` defines; reports a parse error in its JSON | Baker (the report) and the stacks (the command) |
| 0004 | `check`'s JSON holds every reason it fails: shape errors and warnings, and a pass field | Baker (the report) and the stacks |
| 0001 | `eval` for a game that is not running answers in the console's JSON | the stacks |
| 0002 | `sim`'s results as one JSON document, and a comparison of two runs | the stacks |
| 0003 | `promote`'s preview as a unified diff that applies to exactly what `--write` writes | the stacks |

**Why most of it is the stacks'.** Baker has no `sim`, no `eval` command, and no
`promote` command; those are the stacks' commands over Baker's API. Baker's part is the
check report, which every host that gates on `check` shares.

**Order.** 0004 and 0005 first (the gate), then 0001, 0002 and 0003, each its own commit.

---

## 1. Baker: the check report, version 1.1

### The finding behind 0005's second part

- `check` already reports an undefined call inside a `{func}` body that is in the trees it
  is given. That was tested before this note.
- The commands that `#autorun` runs, though, are kept apart from the merged tree
  (`Config::autorun()`, D107), so a check of `cfg.root()` never sees them.
- Any host that checks a Config has the same gap. So Baker gains a Config form:

```cpp
// Checks a Config's data as it loads: the merged tree, and the #autorun commands of its
// enabled layers (which are not in the tree), with its read session's packs unless given.
// `extra` adds trees a host checks alongside (scenarios, metrics).
void check(const Config& cfg, const Context& ctx, CheckReport& report,
           CheckOptions options = {}, std::span<const Array* const> extra = {});
```

The autorun commands are checked as one tree built from them. Their arrays keep their
files and lines, so demand is located as usual.

### Problems the host found, and a rule for passing

```cpp
struct CheckReport {
    ...existing...
    // Filled by the host, not by check: problems loading the data reported (a file that did
    // not parse, an #autorun command that failed), and the shape check's results.
    List<Error> load;
    List<Error> shape_errors;
    List<Error> shape_warnings;   // a field no shape declares: never a failure
    // Whether a call to a function nothing defines (not a stub) fails the check, as an
    // agent's gate wants: set from CheckOptions::unresolved_fails.
    bool unresolved_fails = false;

    // No errors, no load problems, no shape errors, and, under unresolved_fails, no
    // unresolved demand.
    [[nodiscard]] bool ok() const noexcept;
};
```

- **Who fills what.** Baker's `check` fills what it fills today, plus `unresolved_fails`
  from `CheckOptions`.
- **Why the host fills the rest.** The host holds the load and the shapes, and
  `baker::script` does not depend on `baker::shape`.
- **What `ok()` means now.** It was "no errors". It now means all of the above. Every
  caller in the repository is checked: the meaning only widens when a host fills the new
  lists.

### The JSON

- **Schema** `baker.check/1.1`: a minor version, adding fields as `docs/check-json.md`
  allows.
  - `"passed"`: `ok()`, first after `schema`, so a reader need not re-derive the rule.
  - `"load"`, `"shape_errors"`, `"shape_warnings"`: in the problem form of `errors`, sorted
    the same way.
- **The pinned example.** `tests/script/check_expected.json` is updated, and the docs
  describe the new fields.

**Acton note.** This is tool path. Three `List`s and a `bool` are added to a report
struct that is built once per check; nothing runs at frame time.

---

## 2. The stacks' `check`

- **Load problems.**
  - `Live` keeps the located `Error`s that its load reported, beside its text log: a layer
    that did not read, and each failed `#autorun` command. Validator warnings stay out:
    they are the shape and call checks again, which have their own lists.
  - `check` copies them into `report.load`.
- **A parse error.** When a data file does not parse, `check` now prints the JSON, with
  `passed: false` and the error in `load`, before exiting 2. That keeps the note's
  constraint that exit statuses keep their meanings; 2 is still "could not load".
- **Autorun.** `check` uses the Config form, with the scenarios and metrics as `extra`.
- **Shapes.** `shape_errors` and `shape_warnings` are filled from `shape::check`. Standard
  error keeps its text for people.
- **The rule.** `unresolved_fails = true`, and the exit status is `report.ok() ? 0 : 1`.

---

## 3. `eval --json` (0001)

- **The output.** `<game> eval --json "{cmd}"...` answers each command with one
  `baker.reply/1` line. It goes through `eval_request`, with the request form
  `(N {cmd})` and N counting from 1.
- **Exit status.** 0 when every command ran, 1 when a reply carries an error, 2 when the
  game could not load.
- **No window, audio or frame loop.** `eval` already starts none of them.

---

## 4. `sim` as JSON, and `compare` (0002)

- **Schema `stacks.sim/1`.** `sim` writes `results.json` in `--out` beside the `.dta`
  files, and prints the same JSON to standard output with `--json`. It is the stacks'
  schema, not Baker's, so it is named for the stacks. It holds:
  - `runs`: scenario, seed, steps, `passed`, `errors`, and `objectives`, each with `name`,
    `value` (`null` when not finite, with the reason in `error`), `pass` and `error`;
  - `summary`: per objective, `passed`, `failed`, `min`, `median` and `max`;
  - `status`: `"passed"`, `"failed"` or `"error"`, which is the exit status's meaning.

  Runs are sorted by scenario, then seed, and objectives by name. There are no times and
  no absolute paths, so the same data gives the same bytes.
- **`compare`.** `<game> compare BEFORE_DIR AFTER_DIR` reads two `results.json` files,
  with no rerun, and prints `stacks.compare/1`. For each objective it gives:
  - `moved`: `"broke"` (passed, now fails), `"fixed"`, `"same"` or `"new"`/`"gone"`;
  - `before` and `after`, each with `passed`, `failed`, `min`, `median` and `max`;
  - `median_change`;
  - the scenario and seed pairs whose pass changed.

  Exit 1 when anything broke.

---

## 5. `promote --patch FILE` (0003)

- **The preview, computed in memory.** `promote --patch FILE` runs the real promotion
  with the Config's writer set to capture text instead of writing. That is Baker's existing
  writer hook (D132), so nothing is written, created or locked.
- **The diff.** It writes a unified diff of each captured file against the file on disk,
  with paths relative to the data directory. Applying it with `patch -p1` (or `git apply`)
  gives byte for byte what `--write` writes. A test applies it and compares.
- **Refusals.** Promotion is all or nothing, so on any refusal the patch is empty and the
  refusals are printed as today, exit 1.
- **The diff code.** A line-based LCS diff, about 80 lines in the stacks' base. Data files
  are small, and the stacks take no dependency for it.

---

## 6. Tests

- **Baker.**
  - `check(cfg, ...)` finds demand inside an autorun `{func}` body.
  - `ok()` honors each new list and the demand rule.
  - The JSON's new fields are in the pinned example.
- **Stacks**, against a copy of a stack's data in a temporary directory:
  - a failing autorun command exits 1 with `load` filled;
  - an out-of-range value exits 1 with `shape_errors` filled;
  - a parse error exits 2 with JSON;
  - `eval --json` gives one reply line per command;
  - `sim --json` is stable across two runs, and `compare` reports a broken objective;
  - `promote --patch` applies to the same bytes `--write` writes.
