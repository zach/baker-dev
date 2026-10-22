# Syntax experiments: sketches of other text forms for Baker, each on its own branch
# (experiments/README.md). Built only with -DBAKER_BUILD_EXPERIMENTS=ON; nothing here ships,
# and nothing here counts toward the core's line budget (tools/count_lines.py reads src/
# and include/ only).
add_subdirectory(common)

# Every experiments/syntax-*/ directory with a CMakeLists.txt of its own.
file(GLOB _baker_experiments RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} LIST_DIRECTORIES true
     ${CMAKE_CURRENT_SOURCE_DIR}/syntax-*)
foreach(_dir ${_baker_experiments})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/CMakeLists.txt)
    add_subdirectory(${_dir})
  endif()
endforeach()
