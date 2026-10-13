# `check`'s JSON output: schema `moira.check/1`

`moira::write_json(report, sink, &session)` (or `to_json(report, &session)` with
`<moira/std_script.hpp>`) writes a `CheckReport` as one JSON object, so a studio can keep it
as a build artifact and diff it from one milestone to the next (spec §5, D95). The text form
(`write(report, sink)`) holds the same data for people.

```cpp
moira::CheckReport report;
const moira::Array* trees[] = {game.get(), level.get()};
moira::check(trees, ctx, report, {.packs = &cfg.session()});
std::ofstream("check.json") << moira::to_json(report, &cfg.session());
```

`tests/script/check_expected.json` is a complete example, checked byte for byte by the test
suite.

## Stability

- **The same data gives the same bytes.** Every list is sorted by its data alone (below);
  nothing depends on memory addresses, hash order, or time.
- **Files are named as the loads named them.** A path given relative to the working
  directory stays relative; Moira adds no absolute paths.
- **Formatting:** UTF-8, two-space indentation, one list entry per line, a newline at the
  end. Strings escape `"`, `\`, and control characters (`\n`, `\t`, `\u00XX`); other
  characters are written as they are. Numbers are integers.
- **Versioning.** `schema` is `moira.check/MAJOR` while the minor version is 0, then
  `moira.check/MAJOR.MINOR`. A field is only ever added with a new minor version, which old
  readers can ignore; a field is removed or renamed, or its meaning changed, only with a new
  major version.

## The object

| Field | Type | Contents |
|---|---|---|
| `schema` | string | `"moira.check/1"` |
| `errors` | list of problems | calls with the wrong arguments for every overload |
| `warnings` | list of problems | the one-way rule: a harder pack calling a softer one's function (D83) |
| `demand` | list of demands | functions and methods called but not defined, or stubs |
| `repeats` | list of repeats | subtrees repeated across the trees of one `check` call (D94) |
| `findings` | list of problems | informational: packs that declare one owner (D94) |
| `packs` | list of packs | the session's packs and their harness metadata; empty without a session |

**A problem** (errors, warnings, findings), sorted by `file`, then `line`, then `message`:

| Field | Type | Contents |
|---|---|---|
| `code` | string | the `ErrorCode`: `syntax`, `range`, `directive`, `io`, `limit`, `type`, `not found`, `refused` |
| `file` | string | where it was found; `""` if unknown |
| `line` | integer | the line; 0 if unknown (a pack's finding names only its file) |
| `path` | string | the array path that led there, as errors print it; `""` if none |
| `message` | string | what is wrong, as the text form says it |

**A demand**, sorted by `name`, then `on`:

| Field | Type | Contents |
|---|---|---|
| `name` | string | the function or method called |
| `on` | string | for a method, the class of the object it was sent to; else `""` |
| `stub` | boolean | true: the function exists with status `stub`; false: nothing defines it |
| `sites` | list of sites | every call, sorted by `file`, then `line` |

**A repeat**, in the order `find_repeats` gives (first site, then size, largest first, then
text):

| Field | Type | Contents |
|---|---|---|
| `form` | string | one occurrence, as DTA text |
| `nodes` | integer | its size, counted recursively |
| `sites` | list of sites | every place it appears, sorted by `file`, then `line` |

**A site:** `file` (string) and `line` (integer).

**A pack**, sorted by `name`, then `file`:

| Field | Type | Contents |
|---|---|---|
| `name` | string | the pack's name |
| `level` | string | its level among the session's pack levels |
| `file` | string | the file that declares it |
| `requires` | list of strings | `(requires ...)` |
| `owners` | list of strings | `(owners ...)` |
| `for` | list of strings | `(for ...)` |
| `stage` | string | `genesis`, `custom`, `product`, `commodity`, or `""` if not declared |
