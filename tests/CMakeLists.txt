foreach(addon test_basic test_defaults test_bad_default)
  anybind_add_module(${addon} ${addon}.cpp)
  # The generator expression keeps multi-config generators (Visual Studio)
  # from appending a per-configuration subdirectory.
  set_target_properties(${addon} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/tests>")
  target_compile_options(${addon} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>)
endforeach()
