# The instruction-count driver (tools/count_instructions.py): one small program built against
# any Baker tree, so the same cases run on two commits.
cmake_minimum_required(VERSION 3.21)
project(baker_count CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BAKER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "The Baker tree to count")
add_subdirectory(${BAKER_ROOT} baker)
add_executable(baker_count count.cpp)
target_link_libraries(baker_count PRIVATE baker::baker)
if(TARGET baker::host)
  target_link_libraries(baker_count PRIVATE baker::host)
  target_compile_definitions(baker_count PRIVATE BAKER_COUNT_HOST=1)
endif()
