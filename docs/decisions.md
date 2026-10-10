# Decisions

Deviations from and clarifications of `docs/spec.md`, with the reasoning. Short entries,
newest last. Background documents live in `docs/background/`.

---

## D1. `#merge` fills missing keys; Config layers override

*Spec §4, Appendix A.*

The spec calls the slide's merge example — `(a (b 1))` merged with `(a (b 2) (c 2))`
giving `(a (b 1) (c 2))` — a typo, and asks for last-write-wins. Re-reading Appendix A,
the slide is consistent with how the original was used: the *app-level* file `#merge`s
`default.dta`, so the app file is loaded first and defaults are merged *into* it. With
that direction, first-write-wins is exactly what lets the app file override. Community
format notes describe `#merge` the same way.

So we have both, explicitly:

- **`#merge` directive in a data file:** fill-missing. Existing keys keep their value,
  missing keys are inserted, subarrays present on both sides recurse. Faithful to DTA, so
  files written for the original behave the same.
- **`Config` layers (`push_layer`, `load`/`merge` shorthands):** last-write-wins. Layers
  merge bottom to top and higher layers override leaves.

One merge routine with a policy flag, not two implementations. The source carries a
comment pointing here so the next person who checks the slide knows why.

## D2. The line budget is a soft target

*Spec §0, M10.*

4,000 non-test, non-comment lines is tracked and reported (`tools/count_lines.sh`,
printed in CI per target), not enforced. The full scope — edit-mode write-back, layers,
telemetry, `check` and its reports, coroutine tasks, binary cache — is expected to exceed
it. Smallness still matters: every milestone records its line count here, and growth that
looks disproportionate gets called out rather than absorbed.

## D3. No Harmonix files in the repository or CI

*Spec §8, M1.*

Genuine Harmonix `.dta` files are Harmonix copyright, which conflicts with keeping the
repository cleanly MIT. We vendor none and CI fetches none.

- CI's corpus test runs over **original fixtures** in `tests/corpus/`, hand-written to
  exercise the same syntax real files use (quoted symbols, macros, `#ifdef`, `#merge`,
  commands, properties, odd comments, hex ints, etc.).
- `tools/parse_corpus` (M1) points the parser at any local checkout, e.g. of
  `mariteaux/milo-script-library`, for a manual 100% parse check. The M1 "real corpus at
  100%" gate is satisfied by running it locally and recording the result here.
- The fuzzer is seeded from the original fixtures.

## D4. `Object` nodes hold a salted handle, never a pointer

*Spec §1 table vs. §5; `background/halo-tags-vs-dataarray.md` §6.*

The §1 table says `Object*`; §5 says a 32-bit salted handle. The handle wins. In
`moira::data`, `Type::Object` is an opaque 32-bit handle (16 index + 16 salt) that copies,
compares, prints, and round-trips but cannot be resolved; `moira::data` knows nothing
about `Context`. `moira::script` owns the per-`Context` object table and does strong
(`get`) and weak (`try_get`) resolution.

## D5. Handler cache: rewrite slot 0 in place

*Spec §5, "Handler caching"; §2 layout targets.*

The array header has no room for a cached handler plus generation. As in the original,
first execution rewrites the command's slot 0 from `Symbol` to `Func`, pointing at the
handler record (which keeps the name, so printing, write-back, and error messages are
unchanged). The registry generation is stored in the node's 3 spare padding bytes (24
bits); a mismatch re-resolves. Node and header sizes are unchanged.

Consequence to keep in mind: execution mutates the array. That conflicts with
"multi-thread-read-after-freeze" for *script* arrays executed concurrently, and with
macros sharing arrays across sites (harmless: the rewrite is idempotent and
name-preserving). If either bites, or the M4 benchmark shows the cache doesn't pay, drop
it and record that here.

## D6. Tags are Symbols only

*Spec §3.*

`find` matches only arrays whose first node is a `Symbol`. Lookup stays a pointer
comparison, and "same tag" means the same thing everywhere: lookup, merge, sorted mode,
provenance, write-back, and shapes. Accepting String tags too would make merges silently
duplicate entries whenever a key was spelled one way in the base and the other in an
override. Real data overwhelmingly keys by symbol; string-first arrays are mostly ordered
display lists and plain string lists.

Keys that aren't bare symbols use the quoted form, `'bacon and eggs'`. The spec's menu
example is corrected accordingly. To catch the mistake this invites, a *failed* lookup
checks for a String-tagged child with the same text and suggests the quoted-symbol
spelling in the error. That check is on the error path only.

## D7. Background documents

The spec's `reading-a-studio.md` was supplied as `harness-lens-on-development.md`; it is
stored under the spec's name. All six companion documents are in `docs/background/`.

## D8. Known extension point: epoch-cached pure commands

*From `background/seexpr-vs-moira.md` §8.* A command whose inputs are all tweaks is
constant until reload and could be memoized on the reload epoch if its functions were
marked pure. Not built — it adds a purity annotation and an evaluation cache for a
workload Moira doesn't have. Recorded so it isn't reinvented by accident.

## D9. M0 build layout

- Three CMake targets: `moira_data` (`moira::data`), `moira_script` (`moira::script`,
  links `moira::data` PUBLIC), and the interface umbrella `moira` (`moira::moira`).
  `MOIRA_BUILD_SCRIPT=OFF` builds the data layer alone; CI has a job that does exactly
  that, so a reverse dependency fails the build.
- Build switches: `MOIRA_BUILD_SCRIPT`, `MOIRA_BUILD_TESTS`, `MOIRA_ATOMIC_REFCOUNT`,
  `MOIRA_SHIPPING`, `MOIRA_NO_EXCEPTIONS`, `MOIRA_SANITIZE` (`address,undefined` etc.).
  Feature switches become compile definitions on the library targets.
- doctest 2.4.11 vendored under `third_party/doctest/` (MIT), recorded in
  `THIRD_PARTY.md`.
