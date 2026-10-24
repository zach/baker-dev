# Fuzzer regressions

Inputs on which `baker_parse_fuzzer` once found a bug, one file each (`*.bin`, raw
bytes). The data-layer tests run the fuzz property over every file here.

| File | Bug |
|------|-----|
| `bom_inside_symbol.bin` | A symbol starting with UTF-8 BOM bytes was printed bare at the very start of a file, where the reparse skipped the BOM. |
| `bom_symbol_with_quotes.bin` | The first fix (quoting such symbols) broke bare atoms that also contain `'`. Final fix: `to_dta` never begins with BOM bytes; it adds a leading space. |
| `quoted_remove_symbol.bin` | A quoted `'#remove'` outside a key's only value was printed bare, where the reparse refused it (D143). Fix: `#remove` is bare only in the slot where the parser takes it. |
| `quoted_layer_verbs.bin` | The layer verbs' slot rule: a quoted `'#append'`, `'#drop'` or `'#was'` prints bare only where the parser takes it, and quoted elsewhere. Writing it found a harness bug the `#remove` marker (D143) already had: the span check read a marker's token alone, where it is an error. Fix: the check skips a directive token. |
| `quoted_marker_as_tag.bin` | Found by a dictionary-driven random driver over the parse property (libFuzzer's runtime was not installed): a quoted `'#append'` as an array's first node, in an array holding a marker deeper down, was printed bare, where the reparse refused it. Fix: the slot rule never takes a marker for a tag. |
| `symbols_hash_alike.bin` | The symbols `''` and `'\x01'` had one name hash, and the cache write refused them (D148). Fix: the checksum folds the length in after the bytes. |
| `macro_sharing_budget.bin` | Three nested macros make a tree of about 82,000 nodes from 164 bytes. The first read counts each use once and stays under the harness session's 65,536-node limit, but the print spells every node, so its reread passed the limit; the harness skipped only trees over 100,000. Fix: the harness skips a tree larger than the session's own node limit. |
