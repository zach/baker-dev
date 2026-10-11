#!/bin/bash
# Builds and tests every configuration that can be built locally, one line each. Run it
# before committing: CI has more (MSVC, libc++, TSan), but most breakage shows here first.
# Build directories go under $MOIRA_MATRIX_DIR (default: build-matrix/, ignored by git).
#
# Clang's ASan runtime is often not installed; the asan configuration uses GCC.
# The eastl configuration needs network access the first time, to fetch EASTL.
set -u
cd "$(dirname "$0")/.."
root=${MOIRA_MATRIX_DIR:-build-matrix}
declare -A cfg=(
  [debug]="-DCMAKE_BUILD_TYPE=Debug"
  [release]="-DCMAKE_BUILD_TYPE=Release"
  [cxx23]="-DCMAKE_CXX_STANDARD=23"
  [ship]="-DMOIRA_SHIPPING=ON"
  [asan]="-DMOIRA_SANITIZE=address,undefined"
  [clang]="-DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release"
  [data]="-DMOIRA_BUILD_SCRIPT=OFF"
  [plain]="-DMOIRA_ATOMIC_REFCOUNT=OFF"
  [eastl]="-DMOIRA_USE_EASTL=ON"  # fetches EASTL at configure time
)
status=0
for n in "${!cfg[@]}"; do
  dir=$root/$n
  [ -d "$dir" ] || cmake -S . -B "$dir" -G Ninja ${cfg[$n]} >/dev/null 2>&1
  errs=$(cmake --build "$dir" -- -k 0 2>&1 | grep -cE "error:|warning:")
  res=$(UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$dir" -j8 2>&1 | grep -E "tests passed")
  echo "$n: $errs errors/warnings; $res"
  [ "$errs" = 0 ] && [[ "$res" == 100%* ]] || status=1
done
exit $status
