foreach(addon test_basic test_defaults test_bad_default test_ts test_registry test_handles test_stl test_bigint test_struct)
  anybind_add_module(${addon} ${addon}.cpp)
  # The generator expression keeps multi-config generators (Visual Studio)
  # from appending a per-configuration subdirectory.
  set_target_properties(${addon} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/tests>")
  target_compile_options(${addon} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>)
endforeach()

# Code that must be rejected at compile time. Excluded from the default build:
# tests/compile_fail.test.mjs builds each target and expects cases 1..6 to fail
# and case 0 (the same bindings with owning types) to succeed.
foreach(case RANGE 0 6)
  anybind_add_module(compile_fail_${case} compile_fail.cpp)
  set_target_properties(compile_fail_${case} PROPERTIES
    EXCLUDE_FROM_ALL ON
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/tests/compile_fail>")
  target_compile_definitions(compile_fail_${case} PRIVATE ANYBIND_CF_CASE=${case})
  target_compile_options(compile_fail_${case} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>)
endforeach()
