# Fuzzer regressions

Inputs on which `baker_parse_fuzzer` once found a bug, one file each (`*.bin`, raw
bytes). The data-layer tests run the fuzz property over every file here.

| File | Bug |
|------|-----|
| `bom_inside_symbol.bin` | A symbol starting with UTF-8 BOM bytes was printed bare at the very start of a file, where the reparse skipped the BOM. |
| `bom_symbol_with_quotes.bin` | The first fix (quoting such symbols) broke bare atoms that also contain `'`. Final fix: `to_dta` never begins with BOM bytes; it adds a leading space. |
| `quoted_remove_symbol.bin` | A quoted `'#remove'` outside a key's only value was printed bare, where the reparse refused it (D143). Fix: `#remove` is bare only in the slot where the parser takes it. |
| `quoted_layer_verbs.bin` | The layer verbs' slot rule (dev/syntax-layer-markers-design.md): a quoted `'#append'`, `'#drop'` or `'#was'` prints bare only where the parser takes it, and quoted elsewhere. Writing it found a harness bug D143 already had: the span check read a marker's token alone, where it is an error. Fix: the check skips a directive token. |
| `quoted_marker_as_tag.bin` | Found by a dictionary-driven random driver over the parse property (libFuzzer's runtime was not installed): a quoted `'#append'` as an array's first node, in an array holding a marker deeper down, was printed bare, where the reparse refused it. Fix: the slot rule never takes a marker for a tag. |
| `symbols_hash_alike.bin` | The symbols `''` and `'\x01'` had one name hash, and the cache write refused them (D148). Fix: the checksum folds the length in after the bytes. |
