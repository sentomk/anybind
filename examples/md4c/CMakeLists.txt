cmake_minimum_required(VERSION 3.21)
project(anybind_md4c LANGUAGES C CXX)

include(FetchContent)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/anybind.cmake")

FetchContent_Declare(
  md4c
  URL https://github.com/mity/md4c/archive/refs/tags/release-0.5.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # Skip md4c's own CMakeLists.txt (uses cmake_minimum_required 3.5 and forces /MT on MSVC).
  SOURCE_SUBDIR _skip_cmake
)
FetchContent_MakeAvailable(md4c)

# Compile only md4c's core parser and HTML renderer as a hidden PIC static lib.
add_library(md4c_core STATIC
  "${md4c_SOURCE_DIR}/src/md4c.c"
  "${md4c_SOURCE_DIR}/src/md4c-html.c"
  "${md4c_SOURCE_DIR}/src/entity.c"
)
target_include_directories(md4c_core PUBLIC "${md4c_SOURCE_DIR}/src")
set_target_properties(md4c_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
)

anybind_add_module(md4c md4c_binding.cpp)
target_link_libraries(md4c PRIVATE md4c_core)
set_target_properties(md4c PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>"
)
target_compile_options(md4c PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
