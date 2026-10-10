# Two Data Systems, 2004–2005

A comparison of Bungie's **tag** system (Chris Butcher, Game Tech 2004) and Harmonix's
**DataArray** (Eric Malafeew, GDC 2005), and what the comparison implies for Moira.

Sources: Butcher's speaker notes and slides, Malafeew's slides and session audio. Butcher
ran out of time and skipped the entire runtime-data half of his talk; the notes contain it
in full, so parts of this reference material were never actually spoken aloud.

**We are not implementing the Halo system.** This document exists to sharpen the
Moira design by contrast, and it ends with four specific changes to the spec.

---

## 1. The systems are solving opposite halves of the same problem

| | **Halo tags** | **DataArray** |
|---|---|---|
| Unit | One asset = one typed file | One tree of untyped nodes |
| Schema | Mandatory. Each tag type *is* a C struct | None. Schema is whatever the reader asks for |
| Authority | The code definition | The data file |
| Wire format | Binary, matches memory layout | Text, with an optional binary cache |
| Scale | 11.6 GB, 39,000 files, 130 types | Kilobytes to a few MB of config and script |
| Scripting | Separate system; data is data | Fused — scripts live inside the data |
| Editing | Generated GUI (Guerilla) | Text editor, plus an in-game param cycler |
| Cost | ~12 years of evolution, a 17-engineer team with 5 dedicated test engineers building farms | ~70 KB of code, ~30 KB excluding built-ins |

Halo's system is a **serialization and asset-management architecture**. Harmonix's is a
**configuration and behavior language**. Both are correctly described as "data-driven," and
if you tried to swap them you would get a disaster in either direction. The lesson to take
into Moira is that the right answer is determined by scale and by who is editing, not by
taste.

---

## 2. Schema: the central disagreement

Halo's tag definitions are macro tables sitting beside the C struct:

```c
struct ai_properties {
    word      flags;
    short     ai_size;
    string_id type_name;
    real      leap_jump_speed;
};

TAG_BLOCK(ai_properties_block, 1, sizeof(struct ai_properties), NULL, NULL) {
    {_field_flags,     "ai flags", &ai_properties_flags},
    {_field_enum,      "ai size",  &ai_size_enum},
    {_field_string_id, "ai type name"},
    {_field_real,      "leap jump speed"},
    {_field_terminator}
};
```

What that buys, and it is a lot: automatic serialization of arbitrary hierarchies,
byte-swapping, element insert/delete/duplicate, and — the big one — **a complete editing
GUI generated from the definition**, with markup fields to make it presentable. 130 tag
types, all editable by artists and designers, with no per-type UI code.

What it costs, in Butcher's own words:

- Tag files are useless without an exactly matching code definition.
- Code and data must be checked in simultaneously.
- Versioning support is "poor to nonexistent" — you copy-paste the old struct and
  hand-write a v1→v2 conversion routine.
- The workaround was reserving `long pad[8]` in structures for future fields, which defers
  the problem until you need the memory back. A couple of engineers spent a week writing
  automated tooling to find and reclaim those pads.
- Reclaiming a pad field is "a minefield": you cannot tell whether a legacy tag on disk was
  saved when that field meant something else.
- A definition mismatch can go unnoticed if the resulting data transformation is "outwardly
  benign."

Butcher's own conclusion in the Q&A: *"I think it would be better for us to use a
self-describing format."* The engine architect of the most technically celebrated console
game of its generation, asked what he'd change, named the property DataArray already had.

DataArray pays for self-description by giving up nearly everything in the "buys" column.
There is no generated UI, no static validation, no enum checking. A typo is a runtime
error. What it gets in exchange is that **adding a field is free and removing one is
free**, forever, with no migration step — absence is just a default. Over a five-year,
multi-title codebase, that is the difference between a system that accretes cruft and one
that doesn't.

**For Moira:** we are firmly on the self-describing side, and the Halo experience is the
argument for why. But we should steal the one thing Halo's schema gave that we're missing,
in the form we can afford — see §6.

---

## 3. Text vs binary, and why the answer differs

Halo went binary "because it was a closer map initially to the structures we had been
using," and stayed binary for a hard reason: a development build loads ~8,000 individual
tag files, and it already takes 1–4 minutes. A text format at that volume would not load in
interactive time at all.

DataArray is text-first with an optional binary cache, which is the arrangement Moira
inherits. That is only affordable because the data is small. Config and script are
kilobytes; Halo's bitmaps and sounds are gigabytes.

The interesting convergence is in what each does at ship time. Both build a **flattened,
pointer-rebased, load-in-one-read** binary artifact:

- Halo compiles a level into a cache file whose global resource buffer maps to a known
  virtual base address, so pointers are pre-rebased and there is *no fixup pass at all* —
  two synchronous reads and the level is loaded, ~700 ms. (The one exception is Havok,
  which stores vtable pointers in tag data and forces a vtable fixup. Butcher flags it as
  the sole violation and says they'd like to eliminate it.)
- DataArray serializes the expanded tree into a cache to skip text parsing.

Moira's §9 binary format sits between these. Worth considering: Halo's *fixed virtual
base address* trick is why their load needs zero fixups, and we specified a fixup pass. A
fixup pass is the right call for a library that can't dictate the host's address space —
but note in the source comment that this is a deliberate concession, not an oversight.

---

## 4. Hot reload: same goal, two different safety strategies

This is where the two systems are most alike in ambition and least alike in mechanism, and
it is the most instructive comparison in the whole document.

**Halo's approach — forbid the dangerous reference.**

- Reloading a tag builds a completely new copy in memory and discards the old one,
  repointing a global handle.
- Therefore: *the game may never store a pointer into tag memory.* All access goes through
  a handle (an index into a global table) plus array indices. A character playing an
  animation stores a handle to the animation graph and indices, never a pointer to
  animation data.
- Reload is transitive: if a tag's postprocess derived state from a dependency (an object's
  bounding sphere computed from its render and collision models), changing the dependency
  forces a reload of the dependent tag *all the way back to disk*, because the
  postprocessing context changed even though the bytes didn't.
- Systems get a callback and choose: migrate their runtime state onto the new data, or
  **punt** — give up, be deleted as an entity, and be recreated from the starting state.
  The default is to punt on everything, and specific fields get hand-migrated when someone
  complains. "It's a 10-minute fix to go in and make sure you handle that field changing."
- Some tags are marked non-recoverable; changing those tears down and restarts the level,
  preserving camera and player position.
- Crashing on reload is a bug that must be fixed. What makes that tractable is that the
  automated crash dump records that a tag was recently reloaded, which usually points
  straight at the offending cached assumption.

**DataArray's approach — keep the old data alive.**

Reload a portion of the database, notify dependent C++, done. Nodes are refcounted, so
anything mid-read holds the old array alive and nothing dangles. Parameters are grouped by
the code that owns them, which makes the notification unit meaningful.

**The difference is derived state.** Halo's tags produce expensive postprocessed
derivatives, so a reload invalidates a dependency graph and the handle discipline is what
makes the invalidation safe. A config value has no derivatives, so refcounting suffices.
Moira is correct to take the cheap approach — but if we ever cache derived data from
config (a parsed curve, a compiled lookup table), we inherit Halo's problem and will need
Halo's answer.

**The payoff both teams got is the same, and it is the real point of both talks.** Bungie:
3–5 second turnaround, and *about 75% of all content was authored by people looking at it
running on an Xbox on a TV next to their desk*. Paint a pixel in Photoshop, hit save, look
at the television, two seconds later the character is updated. Harmonix: change data
without recompiling, often without even rerunning.

Butcher names a cultural consequence that is worth putting on a wall: **everyone on the
team understands that unless the content is in the engine and working, the task is not
complete.** And the double edge — when the engine breaks, you hear about it immediately
from everyone.

---

## 5. Runtime state: the half Butcher didn't get to

Entirely outside Moira's scope, but one idea from it is directly applicable, so:

Halo's **datum array** is a fixed-size pool of elements, each with a two-byte header. An
element is referenced not by pointer but by a 32-bit **datum index**: 16 bits of absolute
array index, 16 bits of *salt*. The salt is stored in the element's header and is
regenerated on each allocation, so a stale reference is detectable rather than silently
wrong. `datum_get` asserts index and salt match; `datum_try_and_get` allows a mismatch and
returns null, which is a weak reference; `datum_try_and_get_unsafe` never asserts and is
"strongly discouraged for everything except reading from network or from disk." Memory is
zero-filled on allocation, garbage-filled on free in debug, and the unallocated portion is
write-protected.

Around this: zero heap allocation at runtime anywhere in the game (except Havok, handled
with a page allocator, garbage collection tiers, and overflow pools that steal from the
render buffer and then from the texture cache); a 4 MB gamestate buffer at a fixed virtual
base address so a save game is essentially a memcpy; and therefore save games that are
interchangeable between debug and release builds, which turns a crash report into
build + save-from-one-second-before + minidump, propagated to your machine and dropped into
a debugger.

---

## 6. What this changes in the spec

Four concrete items, applied to `moira-spec.md`:

1. **Salted handles for object references.** Our `Node` holds a raw non-owning `Object*`,
   which has exactly the stale-reference hazard Halo's salt solves. Objects should be
   referenced by a 32-bit index-plus-salt handle into a per-context object table, so a
   script holding a reference to a destroyed object gets a clean error naming the object,
   not a use-after-free. This is a small change with a large safety payoff and it is the
   single best idea to import.

2. **Bounds-check atomic fields at load.** Halo bounds-checks every atomic field when a tag
   is loaded for gameplay, on the principle that *the game can never be broken by anything
   typed into a data file, including a corrupt file on disk*. That principle should be
   ours too, and it is nearly free given we already parse defensively for the fuzzer.

3. **Migrate-or-punt on reload.** Halo's callback contract — handle the change, or declare
   defeat and be rebuilt from scratch — is better than our `on_change` alone, because
   "punt" is the correct default and is what makes the system usable before anyone has
   written migration code. Our `Config::on_reload` should support both, with punt as the
   documented default posture.

4. **Designer-annotatable metadata.** Halo generated its editing UI from code definitions
   and Butcher names the resulting limitation explicitly: programmer-generated UI means
   designers *cannot annotate the tag* — cannot write "this feature doesn't work" or "good
   values for this number are 0.2–0.5" next to the field. Our metadata can live in the data
   file, which means the person tuning the value can leave notes for the next person. Add a
   `note` field alongside `range`/`step`/`desc`, make it data-only, and surface it in the
   generated reference. This is a genuine advantage of the self-describing approach and it
   costs us nothing.

---

## 7. The thing neither talk says out loud

Halo's data is edited by artists and designers through a generated GUI; almost every
engineer at Bungie is a systems engineer, with only two or three working on leaf gameplay,
because leaf gameplay is built by designers assembling values. Harmonix's scripts, by
Malafeew's own answer in the Q&A, are written "usually [by] programmers."

So the schema-heavy system pushed non-programmers *into* the data, and the schema-free
system kept programmers in the loop. That is the opposite of what you'd predict from the
formats alone, and it is the trade being made: structure and tooling let more people touch
the data safely; flexibility and terseness let fewer people move faster. Neither is a
mistake. Both teams shipped generation-defining games on the choice they made.

For Moira, the audience is programmers, the data is config and behavior, and the scale is
small. DataArray's side of the trade is the right one. We're taking Halo's safety
discipline where it's cheap, and leaving its schema machinery where it belongs — in a
codebase with 39,000 assets and five test engineers to keep the farms running.
