# Rules Between Values, Declared in Data: `(rule ...)`

A design note, agreed with the owner on Oct 22, 2026, answering a need that hosts with
balance-heavy data keep raising. It builds on the key references of
`dev/key-references-note.md`. Results and deviations go into `dev/decisions.md` when it is
built.

## The need

Some rules span several values:
- a unit starts with no more health than its maximum;
- loot weights sum to 100;
- a card's estimated power is within 10% of its tier's ideal.

Games keep these as assertions in code, or compute them into a spreadsheet for a person to
scan. A designer who retunes one value breaks a rule without knowing it. Shapes check one
field at a time; a host may register a validator in C++, but the data cannot say what must
hold.

## The design

- **A shape may carry rules,** each a message and a script expression:

  ```dta
  (shapes
     (unit (at units) (keyed)
        (max_hp int (range 1 9999))
        (start_hp int)
        (rule "starts at or below its maximum" {<= $start_hp $max_hp}))
     (card (at cards) (keyed)
        (tier (key_of tiers))
        (power float)
        (rule "within 10% of its tier's ideal"
              {and {>= $power {* 0.9 {data tiers.$tier.ideal}}}
                   {<= $power {* 1.1 {data tiers.$tier.ideal}}}}
              (warn))))
  ```

  - A rule governs what its shape governs.
  - Layers merge rules as they merge fields, by message: a later layer may add a rule, or
    soften one with `(warn)`.
- **What a rule reads:**
  - its record's fields, as `$variables`, named by the field (`$max_hp`); a field the
    record lacks is unset, and a rule reading it fails to evaluate unless the expression
    guards it;
  - anything else in the tree under check, by path: `{data PATH}` reads the merged tree
    that the check holds, as `get()` would, with variables expanded in the path. It does not
    read a live Config, so a check of a candidate layer judges the candidate;
  - the game's own functions, when the check runs in the game binary (`mygame check`). In
    a tool that checks without the game, a call to one is demand, as any call is.
- **A failed rule is an error,** located at the record and carrying its message with the
  values its expression read: `units.grunt: starts at or below its maximum ($start_hp 12,
  $max_hp 10)`. `(warn)` makes it a warning. A rule whose expression does not evaluate is
  an error naming the rule and why.
- **Checked on every load and reload,** as the field checks are, so a live edit that breaks
  a rule warns at once, and `check` and CI fail on it.

## Where it is built

`baker::shape` depends only on `baker::data`, and its validator runs on any thread, so it
cannot evaluate script.
- **The shape library** reads rules and gives them to its callers: `Set` holds them, and
  the shapes JSON writes them (`baker.shapes/1.3`).
- **`check` evaluates them,** in the script library, and so does `check_validator`, which
  runs at every push, reload and restore on the game's thread. The host kit's `Live` adds
  that validator, so a game on the kit gets rules with no new call.
- **A data-only build** reads rules and does not evaluate them. It warns once that it holds
  rules it cannot check.

## Cost

- Rules run only in checks, never on a read or a frame.
- A rule is one evaluation per governed record per check: a few hundred nanoseconds each
  (D78's call cost times the calls in the expression), so 3,000 cards with two rules each
  is about a millisecond a reload.
- `{data PATH}` is a lookup in the checked tree. A path whose variables repeat (a card's
  tier) costs a lookup each time; caching is not worth it at these sizes.

## Tests to write

- A rule that holds, a rule that fails (an error with its values), and a `(warn)` rule.
- A rule that does not evaluate.
- A rule reading another table through `{data ...}` and a `key_of` field.
- A layer that adds a rule, and one that softens a rule to `(warn)`.
- A live edit that breaks a rule warns on the next reload.
- A tool checks rules without the game, with game functions as demand.
- A data-only build warns that rules went unchecked.

## Open, decided when built

- Whether a rule may name the variables it reads explicitly, for clearer messages, or
  whether reading what the expression touched is enough.
- A rule over a whole table at once (weights summing to 100) is a rule on the table's own
  shape: its children are its fields. `{sum ...}` over children may need a small built-in.
