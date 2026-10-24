# Fuzzer regressions

Inputs on which `baker_parse_fuzzer` once found a bug, one file each (`*.bin`, raw
bytes). The data-layer tests run the fuzz property over every file here.

| File | Bug |
|------|-----|
| `bom_inside_symbol.bin` | A symbol starting with UTF-8 BOM bytes was printed bare at the very start of a file, where the reparse skipped the BOM. |
| `bom_symbol_with_quotes.bin` | The first fix (quoting such symbols) broke bare atoms that also contain `'`. Final fix: `to_dta` never begins with BOM bytes; it adds a leading space. |
| `quoted_remove_symbol.bin` | A quoted `'#remove'` outside a key's only value was printed bare, where the reparse refused it (D143). Fix: `#remove` is bare only in the slot where the parser takes it. |
| `symbols_hash_alike.bin` | The symbols `''` and `'\x01'` had one name hash, and the cache write refused them (D148). Fix: the checksum folds the length in after the bytes. |
| `bare_remove_marker.bin` | The harness read every literal token alone, and `(key #remove)`'s marker reads only in that slot (D143), so the harness refused a tree the parser and printer handle. Fix: the harness skips the marker's token. |
| `origin_first_form.bin` | A root whose first form is `(origin "...")` held as data (a second origin after the file's own) prints as a declaration the reader consumes, so the print round trip loses it: a tree today's text cannot spell (D91, D13). Found by libFuzzer on the indent-lines branch; it reproduces on main. Fix: the harness skips the print round trip for such a tree; the syntax fix is hash-declarations' `#origin`. |
