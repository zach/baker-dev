#!/usr/bin/env bash
# Reports non-blank, non-comment C++ lines per library, against the soft budget
# (docs/decisions.md D2). Informational; always exits 0.
set -euo pipefail
cd "$(dirname "$0")/.."

count() {
    # Strip // line comments and /* */ blocks, then count non-blank lines.
    cat "$@" 2>/dev/null |
        sed -e 's://.*$::' |
        awk 'BEGIN{c=0} { while (1) { if (c) { i=index($0,"*/"); if (!i) { $0=""; break } $0=substr($0,i+2); c=0 } i=index($0,"/*"); if (!i) break; pre=substr($0,1,i-1); rest=substr($0,i+2); j=index(rest,"*/"); if (j) { $0=pre substr(rest,j+2) } else { $0=pre; c=1; break } } print }' |
        grep -cv '^[[:space:]]*$' || true
}

files_for() { find "$@" -type f \( -name '*.hpp' -o -name '*.cpp' \) 2>/dev/null | sort; }

data=$(count $(files_for src/data) $(files_for include/moira -not -name 'script*' -not -name 'object*' -not -name 'task*'))
script=$(count $(files_for src/script) $(files_for include/moira \( -name 'script*' -o -name 'object*' -o -name 'task*' \)))
total=$((data + script))
echo "moira::data   ${data}"
echo "moira::script ${script}"
echo "core total    ${total} / 4000 (soft budget)"
