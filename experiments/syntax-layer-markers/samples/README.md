# Samples: layers as packages write them today, and with the verbs

The verbs change no base file: no corpus file holds one, and the 68 corpus files read the
same. What they change is the layer a package, a patch or an agent writes over a stack's
base. So each sample is a layer over a real stack file, in two forms:

- `*.today.dta`: the layer as it is written today, restating the base's list whole.
- `*.dta`: the same layer converted by the experiment's printer against the stack's base,
  then given its comments by hand. The command was
  `baker_syntax_layer-markers to-new <x>.today.dta --base stacks/<game>/data/default.dta`.

| Sample | Base | Today: lines / bytes | Verbs: lines / bytes | Data only, today -> verbs |
|---|---|---|---|---|
| `wayfarer/dlc_a` | `stacks/wayfarer/data/default.dta:25` | 5 / 244 | 5 / 229 | 87 -> 54 bytes |
| `wayfarer/dlc_b` | the same | 6 / 297 | 5 / 172 | 91 -> 58 bytes |
| `hearth/cottage_dlc` | `stacks/hearth/data/default.dta:51,61` | 11 / 669 | 10 / 435 | 502 -> 156 bytes |
| `duel/agent` | `stacks/duel/data/default.dta:22,39` | 8 / 390 | 11 / 586 | 111 -> 233 bytes with the notes, 114 without |

"Data only" leaves out comment lines and `;` comments. The Duel layer grows, for two
reasons. Its reasons move from `;` comments, which promote drops, into `(note ...)` rows,
which promote --patch carries. And the `#was 30` pin adds 7 bytes. Without the notes,
the verb and the pin cost 3 bytes over the restated list.

What each sample reads over its stack is tested in `../tests.cpp`:

- Wayfarer's two DLCs compose to `climbing_gloves glider lamp_oil spyglass rope torch`.
  Restated, the second DLC loses the rope (content-updates.md scenario 2).
- Hearth's cottage loses its phone and gains a lamp. Restated, the phone stays, because the
  merge folds keyed tuples by key. The farmhouse gains a third bed; restated, the new bed
  retunes the first one.
- Duel's jab cancels into `jab low_kick palm`, and easy's reaction is 26.
