# Layer verbs beside `#remove`: `#append`, `#drop` and the `#was` pin, a design note

*A syntax prototype on branch `claude/fervent-cray-l1kyf0-syntax-layer-markers`, merged from
two proposals: layer-markers and pinned-patches. Unlike the other syntax prototypes this is
a core change. The verbs are read by Baker's own parser and applied by its merge, so no
front end could fake them. `experiments/syntax-layer-markers/` holds the converter and the
measurements. Nothing here is adopted. Every number below was measured on this container,
which was shared with seven other builds, so wall-clock rows are noisy; instruction counts
are not.*

## 1. Thesis

A layer has one verb today, `(key #remove)` (D143). Every other change it makes is a
restatement. To add `rope` to Wayfarer's items, a package writes the whole list,
`(items climbing_gloves glider lamp_oil spyglass rope)`. A second package that adds `torch`
writes the list again, and whichever is higher wins, so one of the two items is lost
(content-updates.md scenario 2, finding 4). A restated list also brings back the base's
items as they stood when the package was written. Anything retuned below it since then, live
or by a title update, is silently undone. The prototype gives layers two more verbs on
D143's slot machinery: `(items #append rope)` and `(things #drop (phone -2 4.6 180))`. With
them two packages extending one list compose in stack order, and a patch takes out one
element without restating the rest. A row may also name the base value it was written
against, `(reaction 26 #was 30)`, so a stale overlay or agent edit becomes a located `check`
warning at the row instead of a silent revert. Promote writes a removal by deleting the
key's text instead of refusing (D143), and carries each row's `(note "...")` above its
patch.

The owner declined an append marker twice: D138 gap 36, and D142, which chose keyed tables.
This prototype measured the claim behind that choice against the stacks. Of the stacks'
26 lists, 8 are keyed tables and already compose. 15 are value lists: cancels, items,
blocks and climbable. One is a keyed list with a repeated tag (Hearth's farmhouse has two
beds), and 2 are a single value holding the list. For the 16 lists that are not cleanly
keyed, the verb is the only way to add or remove one element without restating the list
(section 5). It serves spec §4's layers, where content packages live (D138), and §1's
write-back, which D132 and D134 built promote on. The spec's own provenance names song
metadata that made downloadable content tractable as the system's best-known use. The cost
is real and is the subject of most of this note. The core grows by 309 lines, so the budget stands at
15,499 of 15,500. Every reader of a layer's own tree must now know the verbs, and the merge
gains an O(dst × operands) step.

## 2. The syntax

### Tokens and forms

The lexer is unchanged. `#append`, `#drop` and `#was`, like `#remove`, are `#name` atoms
already lexed as directives (parse.cpp, `Tok::Directive`). Before this branch they were
"unknown directive" errors, so no text that read before reads differently now. `directive()`
sends all four to `marker()` (src/data/parse.cpp:843-855). `marker()` pushes the reserved
symbol (`#remove`, `#append`, `#drop`, `#was`) when the frame is a parenthesized array whose
first node is a symbol, and marks the frame. When the array closes, one rule,
`marker_form_error` (src/data/markers.hpp:91-119), checks the whole form. The printer uses
the same rule to decide whether a marker may be written bare. A violation is a located
`Directive` error whose hint is the form:

| Form | Rule |
|---|---|
| `(key #remove)` | The marker is the only node after the tag (D143, unchanged). |
| `(key #remove #was w...)` | A pinned removal: one or more pins, then tagged arrays if any. |
| `(key #append v...)`, `(key #drop v...)` | The verb is the first value, with one or more operands of any kind: scalars, commands, untagged arrays, and tagged arrays, which here are elements and not keys (Hearth's `(lamp -2 4 0)`). |
| `(key v... #was w...)` | One or more values, `#was`, then one or more pins. Tagged arrays (metadata such as `(note ...)`) may stand anywhere and are not counted. |

Everything else is an error. That includes a verb anywhere but the first value, `#was`
after a verb, two `#was`, a row with no values before `#was` or none after it, any marker
in `{...}`, `[...]` or an untagged array, and a bare marker at the top level. Each error is
listed in `tests/data/test_layer_markers.cpp` ("every misplaced marker...").

**One deviation from the brief.** The brief asked for exactly as many pins as values. The
prototype takes one or more. The exact count made it impossible to pin any list whose
length the row changes. An agent adding `palm` to the jab's cancels writes
`(cancels jab low_kick palm #was jab low_kick)`, and `write_back_to` with a pin produces
the same row. A count mismatch is now just a stale or a fresh pin, which `check` compares
whole.

**One hole the brief left, resolved.** A row carries one verb. A package whose list both
loses and gains an element (Hearth's cottage pack drops the phone and adds a lamp) writes
two rows of the key, which the merge already applies in order:

```dta
(households (cottage (things #drop (phone -2 4.6 180))
                     (things #append (lamp -2 4 0))))
```

The layer tools used to list only the first row of a tag, as lookups see it. A composing
row is now listed even when it repeats a tag (`collect_leaves`,
src/data/config_write.cpp:118-135). The alternative was several verbs in one row,
`(things #drop (phone ...) #append (lamp ...))`. It would cost about 15 lines in the slot
rule, the merge and `leaf_values`, and is an open question (section 9).

### The tree

A marker is a `Symbol` among the row's nodes; its operands and pins are ordinary nodes.
Nothing is new in the tree: no `Type`, no layout change, no cache format change. The header bit
D143 called `kHasRemoval` is renamed `kHasMarker`. It is the same bit 2, ORed up the same
height pass in `Array::finish` (src/data/array.cpp:64-77), and a cache keeps it as before.
The parser knows when it pushed a marker and passes that to `make_array`, so the parse
path does not scan for one (arena.cpp:103, parse.cpp:585). An array built in C++ is
scanned once in `make_from` and `make_copy` (array.cpp:50, 60). `verb_of()` tests the flag
before anything else (markers.hpp:45), so the merge and the layer tools pay one flag test
on data that holds no marker.

### Semantics (Overwrite merges only)

- **`#append`** puts its operands after the node run below. Two packages that append to
  one list compose in stack order, D144 placement included.
- **`#drop`** removes, per operand, the first node below that is `structurally_equal` to it.
  A tuple is restated byte for byte, `(phone -2 4.6 180)`. An operand that matches nothing
  drops nothing. `check` says so: "'items' has no z to drop", located at the row
  (src/data/config_check.cpp:223-233). A tuple retuned below the patch is the common case
  of that, and it warns too.
- **A key absent below** is resolved against nothing. `#append` creates it from its
  operands, and `#drop` and `#remove` add nothing (`without_markers`,
  src/data/merge.cpp:44-68). That holds at any depth, inside an appended operand too. No
  merged tree holds a marker: lookups, tweak reads, shapes' instance check, the digest
  (D222) and the cache never see one.
- **`#was`** is stripped before the values replace (merge.cpp:191). The values are those
  before it.
- **`#merge`** (FillMissing) skips verb rows as it skips `#remove`. A pinned row read
  through `#merge` keeps its pin for the layer merge that follows.

### What today's forms become, and edge cases

- **No corpus file changes.** None of the 68 holds a marker
  (`experiments/syntax-layer-markers/tests.cpp`, "every corpus file survives"), and the
  round trip is structural identity: 0 faults.
- **Quoting.** `'#append'` quoted is an ordinary symbol to the parser. The printer writes
  a marker bare only in a run the slot rule accepts, and quoted elsewhere (write.cpp:36).
  `tests/fuzz_regressions/quoted_layer_verbs.bin` runs the fuzz property over both. One
  edge remains. A quoted `'#append'` in a verb's slot, in an array that also holds a real
  marker deeper down, is read by the merge as the verb, because the flag ORs up. The
  printer then writes it bare. This is D143's behaviour for `'#remove'` in its slot,
  carried over.
- **Numbers.** `#drop` compares by `structurally_equal`, so `#drop 1` does not drop `1.0`.
- **Empty lists.** `#drop` can empty a list and leave `(cancels)`. A restated empty row
  cannot, because a row with no values replaces nothing. That matters for 7 of the 15 value
  lists, the ones holding a single element (section 5).
- **Notes.** On a value row a `(note ...)` is metadata. On a verb row everything after the
  verb is an operand, so `(cancels #append palm (note "..."))` appends the note. The Duel
  sample puts its note on the jab instead.
- **Macros.** A marker in a row of a macro's body is pushed with the body:
  `#define ADD ((items #append rope))` and then `ADD` read as the row. A body cannot hold a
  bare marker (`#define V (#append)` is an error), so no macro splices a verb into a row of
  its caller.
- **Directives, packs and scenarios** are unchanged.
- **Shapes.** A verb in `(shapes ...)` is not refused. Shapes merge as data (D84), so it
  would apply, but the per-layer shape read (D145) reads shapes through the public tree and
  does not know the verbs. Section 9 recommends refusing verbs under `shapes`.

### The corpus before and after

`stacks/duel/data/default.dta:22` holds `(cancels jab low_kick)`. The agent's layer that
lets the jab cancel into palm, today and with the verb
(`experiments/syntax-layer-markers/samples/duel/`):

```dta
; today
(fighters (kestrel (moves (jab (cancels jab low_kick palm)))))   ; let jab cancel into palm
(cpu (easy (reaction 26)))   ; easy blocked the first low kick too often
; with the verbs
(fighters (kestrel (moves (jab (cancels #append palm)
                               (note "let jab cancel into palm, for the combo test")))))
(cpu (easy (reaction 26 #was 30 (note "easy blocked the first low kick too often"))))
```

`stacks/wayfarer/data/default.dta:25` holds
`(items climbing_gloves glider lamp_oil spyglass)`. DLC A writes `(items #append rope)` and DLC B writes `(items #append torch)`. Stacked, items
reads `climbing_gloves glider lamp_oil spyglass rope torch`. Restated, it reads
`... spyglass torch`, and the rope is lost.

`stacks/hearth/data/default.dta:51-53` and `:61-63` hold the cottage's and the farmhouse's
`things`. The farmhouse has two beds and two bookcases. Today a keyed `(bed 7.5 0 90)`
retunes the first bed, and a restated list does too. `#append` adds a third bed:

```dta
(households
   (cottage (things #drop (phone -2 4.6 180)) (things #append (lamp -2 4 0)))
   (farmhouse (things #append (bed 7.5 0 90))))
```

`docs/language.md:99` shows `(menu (debug_menu #remove))`. Promoted into the base, it is
now a `-` line for the base's `(debug_menu 1)` and its trailing comment, where D143
refused.

## 3. What it keeps and what it changes

| Area | Status |
|---|---|
| Lexer | Unchanged: the verbs are directive atoms. |
| Parser | Changed, smaller: `removal()` and `bad_remove()` became `marker()`, and the slot rule moved to `markers.hpp`, which the printer shares (parse.cpp net −2 lines). The parse path learns of a marker from the token, never by scanning. |
| Printer | Changed: D143's single compare (`#remove` as the second of two) became the slot rule run on arrays that hold a marker. A marker is bare where the parser takes it and quoted elsewhere (write.cpp:30-37). A marker is never a tag, so a quoted `'#append'` heading an array prints quoted (a bug the random driver found, section 8). |
| Write-back: token replacement | Kept, through `leaf_values`. `plan()`'s targets stop at `#was`, so `write_back` of a pinned row replaces its values and keeps the pin (config_write.cpp:268-274). `write_back_to(path, layer, pin)` writes `#was` with the stack's value below the layer, on a new row or after an existing row's values (config_write.cpp:355-359, 470-485). |
| Write-back: a composed path | New refusal (the tools judge's hazard). A composing row has values, but supplies none. `supplier()` and `source()` skip it and name the literal run's layer (config_impl.hpp:449-463). `write_back`, `write_back_to` and `promote` refuse a path that a layer at or above their target composes, located at that row: "'fighters/kestrel/moves/jab/cancels' is composed by layer 'agent': (cancels #append palm); write the base or promote the layer" (config_impl.hpp:467-478, config_write.cpp:410, 463, 541). Writing the composed list there would apply the verb twice. |
| Write-back: inserted children, D34/D132/D183/D226 | Unchanged. Number text and float-over-int behave as before. A pinned new row is rendered through the same printer. |
| Edit-mode source maps (D33, D128) | Unchanged. The parser records a marker's span like any token. The fuzz property's span check read each token alone, and a marker alone is a directive error. D143's `#remove` already had this latent failure, found when this branch put `#remove` in the fuzz dictionary. The check now skips directive tokens (fuzz/parse_harness.cpp:39-41). |
| Tolerant read (D220) | Unchanged: it reads with the same parser. |
| Provenance and `explain` | `source()` names the literal run. `explain` gains `Standing::Composes` with the row's `op`, file and line (config_write.cpp:687-760, 737). `config.get` returns the composed value whole (src/script/config_tools.cpp:137-155). |
| Layers and `#remove` | Generalized. `#remove` may carry a pin, and promote now deletes a removed key's extent instead of refusing (config_write.cpp:375-403). The extent goes with its line when the line holds nothing else (a trailing `;` comment included); otherwise just the extent goes, with the whitespace before it. |
| `check` and the demand report | `check_layer` compares each pin with the stack (`stale_pin`, config_write.cpp:96-107), as a warning, a refusal under `warnings_refuse`, and `LayerEffectEntry::stale`. A file or text layer pushed without a policy sends the same warnings to the handler (config.cpp:556-569), next to the read's warnings that D163 routed. A `#drop` that drops nothing warns. The demand report is unchanged. |
| Shapes and the reference JSON | The instance check is unchanged, since merged trees hold no marker. The per-layer shape read (D145) does not know the verbs (section 9). The reference JSON is unchanged. |
| Console requests | Unchanged grammar. `config.effect` entries gain `(op ...)` and `(stale 1)`, `config.diff` gains `(op_a ...)`/`(op_b ...)`, and `config.explain` gains `(op ...)` and the standing `composes`. They ride in the value, so the `baker.reply/1.1` envelope is unchanged and the prototype does not bump it (section 9). |
| Project file | Unchanged. |
| Binary cache | Unchanged format. The flag rides in the header as `kHasRemoval` did (test: "a layer read from a cache keeps the flag and still composes"). |
| Host | `diff` prints `-> #append palm` and `stale`. `promote --patch` prints each row's `(note "...")` as `# path: note` above the first `---`; git apply skips it (src/host/commands.cpp:247-272, 415-416; tests/host/test_commands.py `layer_verbs`). +18 host lines. |
| Docs and corpus | Would need `docs/language.md`'s "Removing a key" section extended and the cookbook's keyed-tables paragraph rewritten (section 9). Not done on this branch. |

## 4. Cost, measured

### Parse and check, in instructions

`tools/count_instructions.py --base 3817213` (cachegrind, Release, this machine;
instructions per operation, from runs of N and 2N). `check` and `check_text` are new
cases of `tools/count`. They run bench_layers' 400-row overlay over its 2,000-item table,
under every policy rule, as a tree read once and from text:

| Case | Fork point (3817213) | This branch | Change |
|---|---:|---:|---:|
| `parse`: 16 KB of config text, no marker | 1,845,499 | 1,842,230 | −0.18% |
| `check`: the overlay as a tree, no marker | 3,486,660 | 3,562,993 | +2.19% |
| `check_text`: the same from text | 4,580,012 | 4,666,454 | +1.89% |
| `lookup`, `lookup_text`, `set`, `frame` | 203, 365, 623, 197 | the same | 0 |

The parse workload holds no marker, and it got slightly cheaper. The verbs touch the parse
path in two places. A directive token is compared against four names before the existing
directives. And `finish()` takes the parser's flag, where D143 tested every array's second
node for `#remove`.

A check of an overlay with no marker costs 2% more. That is above noise, since instruction
counts are exact. Callgrind on both sides puts it in four places:

- `leaf_values` against `values_of`: +80k instructions over 20 checks.
- The scan of C++-built arrays for a marker in `make_from`: +400k. A variant that skipped
  untagged arrays, the merged root among them, cost 6k more per check than it saved,
  because its test ran on every node. It was reverted.
- `verb_of`: +320k. GCC does not inline it, so each flag test is a call.
- `stale_pin`'s call: +240k.

`Array::finish` is 165k cheaper. Each item could be shaved, by a forced inline or a
call-site flag test. All of those cost lines the budget does not have.

**The pinned check misses the brief's bar.** The brief asked that `check_layer_text` with
every row pinned stay within 2% of the plain check. On the tree (`tools/count` case
`check_pinned`, this branch only), it is 4,109,043 instructions against 3,562,993: **+15.3%**,
about 1,365 instructions per pinned row. Callgrind's difference between the two runs shows
where they go:

- malloc and free: about a third.
- `stale_pin`: 159 a row, with its vector of pins.
- `verb_of` called on rows whose flag is set.
- `Array::make_from` and `merge_into`. A pinned row cannot be shared into the merged tree,
  the way D127 shares a row the merge leaves unchanged, because the merged row must lose
  its pin. So the merge rebuilds every pinned row.

The rebuild is inherent to a pin in the value run, and it is paid by every merge of a
pinned layer, not only by the check. A metadata pin that the merged tree may keep (`@was`,
section 9) would not pay it. From text, the pinned overlay is also 32% more bytes to read
(13,030 against 9,830). Wall clock (below) shows 503–508 µs against 469–475 µs: +7%.

The experiment's `bench` compares the same parser on both sides, since the verbs are DTA.
On the parse benchmark's 4 MB text it read 78.2 MB/s, and 70.3 MB/s on the text
reprinted by `to_dta` (3.97 MB, laid out differently). That is a ratio of 1.055, at a load
average of 14 on 4 cores. `bench-pins` reads an overlay plain and with every row pinned
(`(price 100.5 #was 90.25)`). In instructions (`parse-n` under cachegrind, 2,000 rows), the
plain overlay is 56,900 bytes and 5,970,638 instructions, 105 a byte. The pinned one is
80,870 bytes and 8,303,100 instructions, 103 a byte. A pin costs what its bytes cost. In
wall clock, at 20,000 rows, the plain text read at 67–94 MB/s and the pinned at 58–62 MB/s,
over three rounds. The instruction counts say the gap is not work done. Whether it is noise
on the loaded machine or a lower IPC on the directive path is not resolved here.

### Layers and merge, wall clock

`tools/bench_compare.py --base 3817213 --bench layers parse --runs 5` (D163's 400-leaf
overlay over a 2,000-item table; the median of 5 runs a side, interleaved; ** marks ranges
that do not overlap):

| Row | Fork point | This branch | Change |
|---|---:|---:|---:|
| read_string | 78.4 µs (78.3–79.7) | 75.1 µs (74.8–75.5) | −4.2% ** |
| check_layer (tree) | 366.5 µs (364.1–367.9) | 345.1 µs (344.1–346.1) | −5.8% ** |
| check_layer_text | 461.7 µs (455.4–465.6) | 472.1 µs (468.9–474.9) | +2.3% ** |
| push_layer (tree), no policy | 97.3 µs (91.6–98.0) | 99.1 µs (93.5–100.9) | +1.8% |
| push_layer (tree), policy | 469.4 µs (460.2–471.6) | 480.1 µs (471.1–487.8) | +2.3% |
| push_layer_text, no policy | 178.1 µs (176.6–179.0) | 178.3 µs (172.5–180.4) | +0.1% |
| push_layer_text, policy | 568.0 µs (562.7–580.8) | 583.7 µs (571.8–587.3) | +2.8% |
| push_layer (tree), policy, report held | 99.8 µs (96.0–100.4) | 100.5 µs (99.2–108.1) | +0.7% |
| push_layer_text, policy, report held | 177.2 µs (175.9–182.5) | 182.4 µs (179.6–185.2) | +2.9% |
| LayerChecker::check_text | 454.3 µs (446.1–456.1) | 459.4 µs (457.3–464.8) | +1.1% ** |
| parse: 4.2 MB | 30.6 ms (30.3–34.6) | 31.1 ms (30.5–43.0) | +1.7% |

D163's push of the overlay as a tree, with no policy (80 µs there, 97–99 µs on this
machine), did not move beyond noise. The rows that moved went both ways. `check_layer`
on a tree got 5.8% faster, though it does more instructions, while `check_layer_text`
got 2.3% slower. That reads as code layout and machine load more than work. The
instruction counts above are the measure to trust: +1.9% to +2.2% for a check, nothing for
a parse or a lookup.

The new rows of `bench/bench_layers.cpp`, on this branch only (best of 200, three runs):

| Row | Time |
|---|---|
| check_layer_text, every row pinned (13.0 KB) | 503–508 µs; plain (9.8 KB): 469–475 µs |
| push_layer_text, no policy, every row pinned | 217–223 µs; plain: 172–180 µs |
| merge: a 2,000-value list restated with 2,100 | 47.5–48.1 µs |
| merge: `#append` 100 to a 2,000-value list | 18.2–18.3 µs |
| merge: `#drop` the last 100 of a 2,000-value list | 413.5–414.2 µs |

`#drop` is O(dst × operands) on one array: each operand scans the run and erases. Dropping
100 of 2,000 symbols costs about 9× restating the list, and the restatement already
includes the merge's copy. A patch drops a few elements, never hundreds. If one did,
an index of the run's nodes by hash would make it linear (about 15 lines).

The tweak-read assembly gate (`baker_tweak_codegen`) passes unchanged: merged trees hold no
marker, so the hot read never meets one.

### Lines

`tools/count_lines.py`: the core goes from 15,190 to **15,499 of 15,500** (+309):
`baker::data` +289 and `baker::script` +20. `baker::host` goes from 2,361 to 2,379 of 2,400
(+18). By file: `markers.hpp` +85 (the slot rule, the verbs, the hints), `config_write.cpp`
+97, `merge.cpp` +50, `config_impl.hpp` +26, `config_check.cpp` +17, `config.cpp` +13,
`config.hpp` +6, `write.cpp` +5, `arena.hpp` +1; `merge.hpp` −6, `array.cpp` −3, `parse.cpp`
−2. The brief estimated about 230 of the 310 available. The prototype took all of them,
because three things the brief did not count turned out necessary. `#drop` needs a check
for operands that match nothing. Promote must not write a verb row's tagged operands as
keys. And composing rows that repeat a tag have to be listed. Of the 309, `#was` costs
about 70: `stale_pin`, the push-time comparison, the pin in `plan()` and `write_back_to`,
and its branches in the slot rule, `leaf_values` and the merge. The verbs without it are
about 240.

The experiment (a tool, STL, outside every budget) is 657 code lines: the converter 306,
the CLI 151, its tests 167, the header 33. `tests/data/test_layer_markers.cpp` is 470
lines.

### The stacks' files

Unchanged, byte for byte, since no base file holds a verb. The verbs change the layers
written over them (section 5). The stacks' data files are 11 files, 437 lines and
21,789 bytes, the same before and after: contraption 57 lines / 2,363 bytes, cube 15 / 666,
duel 48 / 3,133, hearth 108 / 5,807, ledger 29 / 1,667 and economy 22 / 1,496, stuntline
40 / 1,959, wayfarer 74 / 3,210, and its regions interior 22 / 754, north_coast 9 / 311 and
south_coast 13 / 423.

### Allocation and layout

None in the node layout. `leaf_values` allocates its result as `values_of` did. `stale_pin`
allocates the pins only for a row whose flag is set. The push-time staleness check runs
only for a layer whose root holds a marker. It merges the layers below the new one once.
Pushing the pinned overlay over the 122 KB table takes 217–223 µs against 172–180 µs for
the plain one. That covers the read of 32% more text, the pin comparison, and the rebuilt
rows.

## 5. Payoff, measured

### The stacks' lists

`baker_syntax_layer-markers lists` classifies every list key in the stacks' data
(cancels, items, things, blocks, climbable, drops, glides, objectives). It writes the layer
text a package would write to add one element and to drop the first, today and with a
verb, and checks what each reads over the file:

| Kind | Lists | Today | With the verbs |
|---|---|---|---|
| Value list: `(cancels jab low_kick)`, `(items ...)`, `(blocks (x z w d h) ...)`, `(climbable ...)` | 15 | Restate the whole list to add or drop one. Two packages that extend it do not compose. A list of one cannot be emptied (7 lists). | `#append` / `#drop`; packages compose; `#drop` empties. |
| Keyed table: Wayfarer's objectives, the cottage's and flat's things | 8 | Already compose by key; `(tag #remove)` drops one. | No verb needed. The converter keeps the keyed form. |
| Keyed with a repeated tag: the farmhouse's things (two beds, two bookcases) | 1 | Neither a keyed child nor a restatement can add a third bed or drop one of two: both reach the first `bed`. | `#append (bed ...)`, `#drop (bed 6 4.6 180)`. |
| One value holding the list: `(glides ((160 16 113) (160 8 146)))`, `(drops (...))` | 2 | Restate the value. | No verb reaches inside a value; restate as today. |

The brief counted 23 lists, all "replaced whole by any layer". The measured count is 26,
and 8 of them are keyed tables that compose today. The case for the verbs rests on the
other 16.

Over those 16 (15 value lists and the farmhouse), the text a package writes to add one
element is **1,428 bytes restated against 840 with `#append`** (−41%), and to drop the
first, **953 against 805 with `#drop`** (−16%). The saving grows with the list: Wayfarer's
forest blocks (7 tuples) go from 171 to 51 bytes. A list of one element costs a few bytes
more with the verb: `(cancels palm new_item)` is 63 bytes nested, `(cancels #append
new_item)` is 66. Every verb reads what the restated list was meant to say: 0 misreads,
tested.

### The samples

`experiments/syntax-layer-markers/samples/` (counts in its README):

| Layer | Today lines / bytes | Verbs lines / bytes | Data bytes |
|---|---|---|---|
| Wayfarer DLC A | 5 / 244 | 5 / 229 | 87 → 54 |
| Wayfarer DLC B | 6 / 297 | 5 / 172 | 91 → 58 |
| Hearth furniture pack | 11 / 669 | 10 / 435 | 502 → 156 |
| Duel agent | 8 / 390 | 11 / 586 | 111 → 233 with its notes, 114 without |

The Duel layer grows. Its reasons move from `;` comments, which promote drops, into
`(note ...)` rows, which promote --patch carries, and the pin adds 7 bytes.

### What each reader gains

- **A designer** writing a DLC states what it adds, so two DLCs no longer race on one list.
  A list retuned in the base after the DLC was written is not reverted by it.
- **An engineer** reviewing a patch sees `-   (debug_menu 1)` for a removal. With D143,
  promote refused and the removal had to be made by hand. The reason for each change is
  printed above the diff.
- **A tool** gets the composition from explain and effect (`op`, `composes`), and a located
  refusal instead of a write that turns an append into a replace.
- **An agent** pins what it tuned from. When the base moves, `check` says "'cpu.easy.reaction'
  was 30 when this row was written; the stack has 32" at the row. Today the stale value
  silently wins.
- **Errors no one can make any more:** losing another package's list entry (scenario 2);
  reverting a retune by restating; retuning the first bed when a third was meant; a
  `#drop` of a retuned tuple that silently drops nothing (it warns); a write-back that
  doubles an append.
- **Smaller diffs:** a DLC's change to a list is one line with one new token. Restated,
  it is a line that holds the whole list.

## 6. Objections

- **"Cost per byte."** Parsing marker-free text costs nothing more: −0.18% in
  instructions (section 4). The merge pays one flag test per level and per tagged child,
  where D143 compared the second node of every child (`is_removal`). Every layer check,
  verbs or not, costs about 2% more instructions. Three things pay more. A drop is
  O(dst × operands). A check of a pinned layer costs 15% more, because the merge must
  rebuild each pinned row. And pushing a pinned layer adds a merge of the layers below.
  *Answered for parsing and lookups. Conceded for checks, wide drops and pins, with the
  fixes costed.*
- **"A second parser."** There is none. The verbs are DTA, read by the same parser, and
  `marker_form_error` is one rule shared by the parser and the printer. *Answered.*
- **"Build time versus run time."** Composition happens at load, in the merge, not in a
  bake. A shipping game that pushes DLC caches pays for `#append` (cheaper than a
  restatement's copy: about 19 µs against 49 µs for a 2,000-value list) and for `#drop`. A
  bake could resolve verbs against a known base (the converter's `resolve` does), but a
  DLC's base is the player's installed stack, which is the reason for the verb. *Conceded:
  the work is at load, by design.*
- **"Solving a problem you don't have."** D142 said keyed tables are the convention, and
  for 8 of the 26 lists they are. The other 16 are value lists or tuple lists that no
  amount of convention makes keyed without renaming every element. (`blocks` would need
  `(b1 (12 32 8 8 5))`, which is worse to write.) Scenario 2 is a research finding, not a
  reported bug: the stacks ship no DLC, so no player has lost an item. *Partly conceded:
  the need is real in the stacks' data and hypothetical in their history.*
- **"It spends the whole budget."** +309 of the 310 lines left. Adopting all of it leaves
  the core one line under its soft budget. *Conceded.* Section 10 proposes a part that
  costs about 240.
- **"Order now matters more."** It does. Two `#append`s read in stack order, so D144's
  placement decides the list's order where it used to decide which list wins. Promote has
  a related wrinkle. It writes a composed list resolved into the target, and if the source
  layer is not then emptied (the host's promote says to empty it), the verb applies a
  second time. A restatement was idempotent. *Conceded and documented: the refusal covers
  layers between the target and the top, but not the source itself.*
- **"Notes in data trip the shapes."** Found by the host test. `(speed 4.5 (note "..."))`
  under `(speed float ...)` fails the shape check: "'speed' takes one float, given 2
  values". The note rides in the merged tree as a child, and the shape counts it. This
  predates the prototype: it is how the inline-metadata convention (D26, D120) meets D84's
  shapes. But it means the agent skill cannot be told to write notes until shapes skip
  metadata children or at-metadata lands. *Conceded; the skill is unchanged.*
- **DTA heritage.** The `#` prefix is already the directive and marker space (D101, D143),
  and the modding community's patch formats use the same three operations (add, remove,
  replace) the verbs give. The corpus is untouched. *Answered.*

## 7. Migration

- **Today's files** need no change: no file holds a marker, and none reads differently.
- **Existing layers** that restate lists can be converted with
  `baker_syntax_layer-markers to-new layer.dta --base base.dta`. A record keeps its keyed
  children. A list becomes `#append`, `#drop`, or both as two rows of the key. A value row
  is pinned, and a run of numbers is treated as a vector, pinned and never appended to.
  `to-dta` resolves verbs back into plain rows, for a Baker without them. That is exact for
  value lists and cannot express dropping a keyed tuple. Both directions are tested over
  the samples and the stacks' lists.
- **No two syntaxes side by side**: it is one syntax with three more directives.
- **The one-way rule (D80)** is about macros and shapes, and the verbs change values, which
  any layer may. A softer pack's `#drop` on a harder pack's list is allowed, as a
  restatement is today.
- **Packs and layers.** Placement (D144) orders composition. `remove_layer` takes a
  package's operands out, which is tested.
- **Live-ops overlays** (D163) are received as text, and the verbs ride in it. The policy
  reads operands: `scalars_only` refuses `#append (lamp -2 4 0)`, and `existing_only`
  refuses `#append` and `#drop` on a key the stack lacks and keeps refusing `#remove`.
  `#was` is optional per row.
- **Producers.** `write_back_to(path, layer, true)` writes a pin. The host's `diff --rows`
  and the journal export do not exist on this branch (path-rows has not landed), so neither
  emits pins yet.

## 8. What the prototype proved, and did not

Built and passing:

- `ctest`: 23 of 23, the experiment's tests included.
- `baker_data_tests`: 253 cases, 14 of them for the verbs
  (`tests/data/test_layer_markers.cpp`). They cover:
  - every form and every misplaced marker;
  - scenario 2 through a Config, with D144 placement and removal;
  - scenario 5 promoted into the base, removal by extent and `#drop` resolved;
  - `source()`, explain's Composes, and the write-back refusal at, below and above the verb;
  - fresh, stale and refused pins, a pinned removal, and the push-time warning;
  - `write_back` keeping a pin and `write_back_to` writing one;
  - two rows of one key, a `#drop` matching nothing, and promote's refusal for keyed
    operands;
  - the policy rules, a cache round trip, a marker inside a new key or operand, and tuple
    drops present and absent;
  - the grep test that no reader of a layer tree uses `values_of`.
- `baker_script_tests`: `config.get`, `source`, `explain`, `effect` and `diff` with the
  verbs. `baker_host_commands`: diff with a stale pin and a removal, and promote --patch
  with the note preamble and a removal, which `git apply --check` takes.
- The fuzz regressions `quoted_layer_verbs.bin` and `quoted_marker_as_tag.bin` pass the
  full parse property: edit-mode spans, cache and print round trips.
- libFuzzer's runtime is not installed on this machine, so in its place a scratch driver
  ran `fuzz::check_parse` over random token sequences from the verbs' vocabulary: brackets,
  bare and quoted markers, rows, macros and comments. On its first 20,000 inputs it found a
  printer bug. A quoted `'#append'` as an array's first node, over a row holding a marker,
  passed the slot rule as a tag and printed bare. It is fixed (a marker is never a tag) and
  kept as `quoted_marker_as_tag.bin`. After the fix, 1,000,000 inputs over five seeds
  pass.
- The experiment: `check` 68 files, 0 faults; `lists` 26 lists, 0 misreads; samples tested
  over their stacks.

Not done, or not proven:

- **No libFuzzer run.** The dictionary gained the three verbs, but the coverage-guided
  fuzzer was not run (see the random driver above).
- **Not run:** `tools/local_matrix.sh` (debug, shipping, sanitizers, EASTL and the rest).
  Only the Release build and `clang-tidy` on the changed files were run; clang-tidy is
  clean. Shipping paths are guarded with `kShipping` as the other write tests are, but were
  not built.
- **The journal export and `diff --rows`** emit no pins.
- **Shapes.** The per-layer shape read (D145) is not routed through `leaf_values`, and no
  refusal of verbs under `shapes` was built.
- **The agent skill** still asks for a `;` comment, because notes fail shape checks
  (section 6).
- **Element paths into a composed list** (`items.4`). `source()` and write-back skip the
  verb, but `explain` reads the composing layer's run raw, so its entry for that layer can
  show `#append` as a value. A fix is about 4 lines, which the budget did not have.
- **Promote of a verb whose operands are keyed tuples** (Hearth's things) is refused, not
  written. The resolved list holds keyed children that `plan()` cannot write as values.
- **`#scale`, `#add` and `#prepend`** are held, as the brief and the cost judge asked.

## 9. Open questions for the owner

1. **Ship `#append` and `#drop`?** Recommended: yes, with the composed-path refusal and
   promote's removal by extent. These are about 240 of the 309 lines, and they relieve the
   16 lists keyed tables cannot.
2. **Ship `#was` now, or as at-metadata's `(@was ...)`?** Recommended: wait for
   at-metadata. The pin and the note are both row metadata. As a value-slot marker the pin
   costs `leaf_values` a branch and every layer reader a rule, about 70 lines. It also
   makes every merge rebuild each pinned row, which a check measures at +15%. A trailing
   attribute that the merged tree may keep would cost none of that, and would give notes a
   form that shapes skip.
3. **Staleness: a warning for file layers and a refusal under `warnings_refuse` (built), or
   a refusal for every text layer?** Recommended: as built. A refusal by default would
   strand every overlay the moment the base moves, which is when a person most needs to
   see it.
4. **Is stack-order composition what designers expect, and does `#drop` by structural
   equality read right on Hearth's lists?** Recommended: yes to the first, since
   placement already orders packages. For the second, yes with the "has no ... to drop"
   warning, which turns a retuned tuple's silent miss into a located message.
5. **Several verbs in one row, or several rows of one key (built)?** Recommended: rows, as
   built. The merge already applies them in order, and they cost no slot-rule change.
6. **`#scale` and `#add`: ever?** Recommended: not until a measured need. A weekend
   multiplier stays a literal written by the live-ops tool. Merge-time arithmetic would
   re-open at load the question D183 settled for write-back: whether `(lives 3)` times 1.5
   stays an integer.
7. **Does the `# path: note` preamble survive the owner's review tools?** Recommended: keep
   it. git apply and patch skip text before the first `---` (tested with
   `git apply --check`), and review tools show it as the patch's description.
8. **Rewrite D142's cookbook paragraph as "key what packages retune by name; mark what they
   extend in order"?** Recommended: yes, if question 1 is yes.
9. **Bump the reply to 1.2?** Recommended: no. `op`, `stale` and `composes` are entries in
   a value, so the envelope is unchanged.
10. **Refuse verbs inside `(shapes ...)`?** Recommended: yes. A shape a softer pack
    composes would slip past D145's one-way check.
11. **Raise the soft budget, or retire something?** Recommended: adopt in part (question 1)
    and keep the budget. The part fits with about 70 lines to spare.

## 10. Recommendation

**Adopt in part.** Take `#append` and `#drop` with `supplier()`'s refusal, `leaf_values`,
the drop warning, and promote's removal by extent and note preamble. That is about 240 core
lines, leaving about 70 of the budget. The stacks show 16 lists that a keyed table cannot
cover and a restatement makes fragile, and the verbs cut a package's text for them by 41%
when adding. Hold `#was` for
at-metadata. The pin is the right idea: a located "was 30; the stack has 32" is the
cheapest guard there is against a stale overlay. But it is row metadata and belongs in
metadata's slot, where the note that travels with it also stops tripping shapes. The next
step is a D-entry for the verbs alone, with the docs (`language.md`'s layer section, the
cookbook's content paragraph) and the shapes refusal. It should be measured on the local
matrix, MSVC included, before it goes in.

## 11. Verification, the raw gate, and review

*Added after the prototype, from the session's review round; `dev/syntax-ideas.md` on
`claude/fervent-cray-l1kyf0` compares all eight branches.* An agent independent of the
prototype rebuilt this branch, reran its tests and reproduced its numbers; a reviewer
judged it under the data-oriented lens (`.claude/skills/acton-review/`) and the tool
author's; and D101's raw gate (464 raw Harmonix files, `baker_parse_corpus --roundtrip
--milo-paths` over a local clone of mariteaux/milo-script-library, nothing from it in the
repository, D3) was run on the branch's build. Where a finding below disagrees with an
earlier section, this section is the later word.

- **Reviewed at** `39f5248`, the branch's final head.
- **Verifier.** Builds; 23 of 23. Line counts per file, every instruction count (parse
  −0.18%, check +2.19%, check_text +1.89%, check_pinned +15.3%), bytes, samples, the
  26-list analysis and the round trip reproduce exactly. Corrections: the Hearth sample's
  cottage half compares against a restated list, but the cottage's things are keyed and
  `(households (cottage (things (phone #remove) (lamp -2 4 0))))` already gives the same
  merged result today; the test meant to prove every layer reader goes through
  `leaf_values` greps for `values_of(`, which no longer exists anywhere, so it guards
  nothing; `bench <file>` fails on every stacks file that holds a `#require`.
- **Raw gate.** 464/464, 0 failures.
- **Technical director, 3 of 5, explore further.** An invariant hole: the marker flag is
  set by the parser, `make_from` and `make_copy` but not by `Array::set`, so an overlay a
  C++ tool builds with `make` and `set` merges with `#append` leaking into the merged
  tree (verified). The +2.19% on a marker-free check is the new per-node scan in
  `make_from`; setting the flag where a marker is made removes it. 11 of the 15 value
  lists are Wayfarer cell geometry no package extends.
- **Tool author, 3 of 5, adopt in part.** The agent loop loses idempotence: after
  `promote --patch` lands, a `(cancels #append palm)` left in `agent.dta` gives
  `jab low_kick palm palm`, silently (verified). A verb row cannot carry `#was`. The Duel
  sample fails Duel's own shape check (`'reaction' takes one int, given 2 values`).
- **Fuzzing.** libFuzzer under ASan and UBSan (the markers are always on), built as CI's
  fuzz job is and seeded with the corpus, the fuzz regressions and the samples: 1,083,088
  executions in 25 minutes, 2,018 edges, no failure, beside section 8's 1,000,000 inputs
  from a random driver. Short of spec §8's 10 million, which belongs to an adoption step.
- **Recommendation after review.** Promote's removal by extent now. `#append` and `#drop`
  when a package that composes a list exists, rebuilt at zero cost when unused (the flag
  set where a marker is made, `Array::set` included), with a warning when an appended
  operand is already present, at about 150 lines; `#was` with at-metadata.
