# Fuzzer regressions

Inputs on which `moira_parse_fuzzer` once found a bug, one file each (`*.bin`, raw
bytes). The data-layer tests run the fuzz property over every file here.

| File | Bug |
|------|-----|
| `bom_inside_symbol.bin` | A symbol starting with UTF-8 BOM bytes was printed bare at the very start of a file, where the reparse skipped the BOM. |
| `bom_symbol_with_quotes.bin` | The first fix (quoting such symbols) broke bare atoms that also contain `'`. Final fix: `to_dta` never begins with BOM bytes; it adds a leading space. |
