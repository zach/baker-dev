# Ledger (game-stacks.md §4.2): a tycoon economy, balanced against bot strategies.
#   stacks_ledger_economy   the economy and the strategies: no window, so its tests link it alone
#   stacks_ledger           the game, and its agent commands (stacks/agent.md)
add_library(stacks_ledger_economy STATIC economy.cpp)
target_include_directories(stacks_ledger_economy PUBLIC .)
target_link_libraries(stacks_ledger_economy PUBLIC moira::moira)
stacks_target(stacks_ledger_economy)

add_executable(stacks_ledger main.cpp)
target_link_libraries(stacks_ledger PRIVATE stacks_ledger_economy stacks_app)
target_compile_definitions(stacks_ledger PRIVATE LEDGER_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stacks_target(stacks_ledger)

if(STACKS_BUILD_TESTS)
  add_executable(stacks_ledger_tests tests/test_economy.cpp)
  target_link_libraries(stacks_ledger_tests PRIVATE stacks_ledger_economy stacks_doctest_main)
  stacks_target(stacks_ledger_tests)
  add_test(NAME stacks_ledger_tests COMMAND stacks_ledger_tests)
  add_test(NAME stacks_ledger_headless
           COMMAND stacks_ledger --headless --frames 120 --console-port 0 --no-vsync)
  add_test(NAME stacks_ledger_sim COMMAND stacks_ledger sim --seeds 1-3)
  add_test(NAME stacks_ledger_check COMMAND stacks_ledger check)
  # economy.dta is generated from economy.xlsx: it must be the export of the sheet checked in.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME stacks_ledger_export_fresh
             COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/export_economy.py --check)
  endif()
endif()
