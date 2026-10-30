#!/bin/bash
# Builds and tests every configuration that can be built locally, one line each. Run it
# before committing: CI has more (MSVC, libc++, TSan), but most breakage shows here first.
# Build directories go under $BAKER_MATRIX_DIR (default: build-matrix/, ignored by git).
#
# --quick runs clang-format, clang-tidy and the shipping configuration alone: the three that
# most often fail after the others pass. Run it first; it is not a substitute for the whole
# matrix.
#
# Clang's ASan runtime is often not installed; the asan configuration uses GCC.
# The eastl and harness configurations need network access the first time, to fetch EASTL,
# Dear ImGui, and GLFW; the harness also needs the X11 and OpenGL development headers.
set -u
cd "$(dirname "$0")/.."
root=${BAKER_MATRIX_DIR:-build-matrix}
declare -A cfg=(
  [debug]="-DCMAKE_BUILD_TYPE=Debug"
  [release]="-DCMAKE_BUILD_TYPE=Release"
  [cxx23]="-DCMAKE_CXX_STANDARD=23"
  [ship]="-DBAKER_SHIPPING=ON"
  [asan]="-DBAKER_SANITIZE=address,undefined"
  [clang]="-DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release"
  [data]="-DBAKER_BUILD_SCRIPT=OFF"
  [plain]="-DBAKER_ATOMIC_REFCOUNT=OFF"
  [eastl]="-DBAKER_USE_EASTL=ON -DBAKER_RTTI=OFF"  # fetches EASTL; no RTTI (D250)
  [harness]="-DBAKER_BUILD_HARNESS=ON"  # fetches Dear ImGui and GLFW; needs X11 and GL headers
)
quick=0
[ "${1:-}" = "--quick" ] && quick=1
status=0
# clang-format over every source file, as CI's format job runs it: first, since it is the
# cheapest, and the matrix's builds cannot see it.
if command -v clang-format-18 >/dev/null; then
  fmt=$(git ls-files '*.hpp' '*.cpp' ':!third_party' | xargs clang-format-18 --dry-run --Werror 2>&1 |
        grep -c "error:")
  echo "format: $fmt files to format"
  [ "$fmt" = 0 ] || status=1
else
  echo "format: clang-format-18 not installed; skipped"
fi
names=("${!cfg[@]}")
[ $quick = 1 ] && names=(ship debug)  # debug only for clang-tidy's compile commands
for n in "${names[@]}"; do
  dir=$root/$n
  # Configured every time (cheap when nothing changed), so a changed flag, or a first
  # configure that failed, is not left behind; its errors count with the build's.
  conf=$(cmake -S . -B "$dir" -G Ninja ${cfg[$n]} 2>&1 | grep -cE "CMake Error")
  errs=$(( conf + $(cmake --build "$dir" -- -k 0 2>&1 | grep -cE "error:|warning:") ))
  res=$(UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$dir" -j8 2>&1 | grep -E "tests passed")
  echo "$n: $errs errors/warnings; $res"
  [ "$errs" = 0 ] && [[ "$res" == 100%* ]] || status=1
done
# clang-tidy over src, as CI runs it (D103), from the debug configuration's compile commands.
if command -v run-clang-tidy-18 >/dev/null; then
  tidy=$(run-clang-tidy-18 -p "$root/debug" -quiet 'src/.*\.cpp$' 2>&1 | grep -cE "(warning|error):.*\[")
  echo "tidy: $tidy findings"
  [ "$tidy" = 0 ] || status=1
else
  echo "tidy: run-clang-tidy-18 not installed; skipped"
fi
[ $quick = 1 ] && exit $status
# The release dry run: the tree as tools/release/publish.py would publish it (dev/releasing.md).
if out=$(python3 tools/release/publish.py check 2>&1); then
  echo "release: $(echo "$out" | tail -1)"
else
  echo "release: $out" | tail -20
  status=1
fi
exit $status
