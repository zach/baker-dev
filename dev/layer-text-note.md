# A Layer as Text for a Peer: `Config::layer_text`

A design note, agreed with the owner on Oct 22, 2026, answering hosts whose server holds a
layer that every client must hold too. Results and deviations go into `dev/decisions.md`
when it is built.

## The need

A server keeps a match's settings as a layer, and a lobby's host may edit them live before
the match starts. Every client must hold the same layer, received over the wire and pushed
with `push_layer_text` under its own `LayerPolicy`.

Today a host can write a layer's tree with `write_dta(*cfg.layer_root(name), sink)`, if it
finds that recipe. But there is no way to send the live edits, and nothing says the round
trip holds.

## The design

- **`Expected<void> Config::layer_text(std::string_view name, TextSink out) const`** writes
  the layer's own tree as text:
  - as read: includes spliced in, macros expanded, removals (`#remove`) kept, comments gone;
  - text that `push_layer_text` reads back to an equal tree.

  It is an error for an unknown layer. `<baker/std_interop.hpp>` gains a `std::string`
  form.
- **`"scratch"`** writes the live edits as a layer: each edited path with its value, and a
  cleared path absent. A lobby host sends its edits as one layer and can go on editing.
  Sending again later sends the edits as they are then.
- **Commands are written as they are.** The receiver's `LayerPolicy` decides: with
  `data_only` it refuses the text, with a located error, as for any text it receives.
- **A reload of the same text is a no-op on the receiver,** as D221 already makes it.
  `layer_text` writes the same bytes for the same tree, so a server that resends unchanged
  settings changes nothing on its clients.
- **Not a write-back.** It writes the tree, not the file, and the layer's file is not
  touched. The text does not keep the source's layout; promotion and write-back are for
  that.

## Cost

Only when called: one walk of the layer's tree, writing it, and allocation only for the
text. A walk of scratch builds its tree from the edits, as a merge does.

## Tests to write

- **Round trip of a file layer** into another Config: an equal tree, the same `get`
  results, and `#remove` kept.
- **Scratch:** after two `set`s and a `clear_scratch`, it carries exactly the remaining
  edits.
- **Repeated pushes:** the same text twice is a no-op on the receiver.
- **Commands:** a layer with commands is refused by a `data_only` receiver, located.
- **Exact floats:** they round-trip exactly (0.1, 1e-300, a tuned 0.7f).
- **Unknown names:** an unknown layer is an error.
