anybind_add_module(test_basic test_basic.cpp)
set_target_properties(test_basic PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
target_compile_options(test_basic PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>)
