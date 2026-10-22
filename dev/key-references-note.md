# A Shape Field That Names a Key: `(key_of TABLE)`

A design note, agreed with the owner on Oct 22, 2026. It answers a need that hosts with
record-heavy data keep raising. Results and deviations go into `dev/decisions.md` when it is
built.

## The need

Game data refers from one record to another by key:
- a unit names its weapon, `(weapon laser)`, which is a key of `(weapons (laser ...))`;
- a card names the card it adds;
- a move names the move it chains into.

Today a mistyped key, `(weapon lazer)`, passes `check`. It fails only when the game looks it
up, often deep into a run. A shape can hold a symbol to a `one_of` list, but that list is
written in the shape. It does not follow the table: a layer that adds a weapon must also add
it to every list that names weapons.

## The design

- **A new field type.** `(key_of PATH)` holds a symbol to the keys of a keyed table, and
  `(list (key_of PATH))` holds each element.

  ```dta
  (shapes
     (unit (at units) (keyed)
        (weapon (key_of weapons))
        (drops (list (key_of items)))))
  ```

- **The table is named by a dotted path,** as `get()` takes it: `weapons`, `world.items`.
  The path is a table's path, not a field's, so its children are its keys.
- **Nested tables resolve to the nearest enclosing one.**
  - A name is looked up outward from the referring record. The first ancestor that holds
    the table wins, and the root is the last place looked.
  - So each fighter's own moves resolve within that fighter:

    ```dta
    (shapes (move (at fighters * moves) (keyed) (next (key_of moves) (optional))))
    (fighters (kestrel (moves (jab (next cross)) (cross))))   ; kestrel's own cross
    ```

  - A path of several segments is looked up the same way, by its first segment.
  - `(at ...)` keeps `*`, which matches where a shape applies. A reference never uses it.
- **Checked against the merged tree,** so a key that a layer adds or removes is honoured with
  no edit to the shape.
- **A key the table lacks is an error.** It is located at the referring field, and names
  the table it looked in and the nearest key: `'lazer' is not a key of weapons (laser?)`.
  An `(optional)` field may be absent, as any optional field may. One that is present must
  name a key.
- **Packs.** A reference into a table that a pack the referrer's pack may not depend on
  supplies (the one-way rule, D80) is a warning, naming both packs, as such a call is
  today.
- **Tools.**
  - `baker.shapes` writes the field's type as `{"key_of": "weapons"}`, a new minor version
    (1.2).
  - `set.shapes()` gives the path, so an editor can offer the table's keys as completions.

## Cost

It is check-time only, with nothing on a read or a frame:
- `check` builds the keys of each referenced table once per check, linear in the data;
- a reference is a lookup in that set;
- nearest-enclosing resolution walks the referrer's ancestors, which a check already holds.

The shape validator runs it at push, reload and restore, as for any shape. A data-only
build has it, since shapes depend only on `baker::data`.

## Tests to write

- A typo is an error.
- A key that a layer adds is accepted, and one that a layer removes is an error at each
  referrer.
- A list of references.
- An `(optional)` reference.
- A nested table: a fighter's move names its own fighter's move, and another fighter's
  key is an error.
- A crossing reference between packs is a warning.
- The shapes JSON names the referenced table.

## Not in this note

- **References to a field's value rather than a key** (a unit's `team` must match a
  `team` value somewhere). That would be R3's rules.
- **Renaming a key and following its references** (a tool's refactor).
