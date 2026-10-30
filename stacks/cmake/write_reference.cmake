# Writes a stack game's reference (`<game> reference --baker-json`) to OUT, after the game is
# linked: the vocabulary tools read to check the stack's data without the game, at the path
# the stack's baker-project.dta names. Run by stacks_reference() in CMakeLists.txt:
#
#   cmake -DGAME=<game binary> -DOUT=<stack>/build/reference.json -P write_reference.cmake
#
# A game whose data does not load writes no reference: the stale one is removed, so a tool
# says the reference is missing rather than checking against an old vocabulary, and the
# build goes on with a warning (the data is not the build's to fix). The file is rewritten
# only when it changes, so a tool watching it reloads only for a new vocabulary.
execute_process(COMMAND "${GAME}" reference --baker-json
                OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  file(REMOVE "${OUT}")
  message(WARNING "${GAME} reference --baker-json failed (${result}); ${OUT} not written:\n${errors}")
  return()
endif()
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
  if(old STREQUAL text)
    return()
  endif()
endif()
file(WRITE "${OUT}" "${text}")
