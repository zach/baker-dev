# What every syntax experiment shares: the corpus, tree comparison, the round-trip test,
# and the parse benchmark against Baker's own reader.
add_library(baker_experiments STATIC experiments.cpp)
target_include_directories(baker_experiments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(baker_experiments PUBLIC baker::data)
target_compile_features(baker_experiments PUBLIC cxx_std_20)
target_compile_definitions(baker_experiments PUBLIC BAKER_EXPERIMENTS_ROOT="${PROJECT_SOURCE_DIR}")
