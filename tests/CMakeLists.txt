foreach(addon test_basic test_defaults test_bad_default)
  anybind_add_module(${addon} ${addon}.cpp)
  set_target_properties(${addon} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  target_compile_options(${addon} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>)
endforeach()
