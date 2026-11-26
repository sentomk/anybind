cmake_minimum_required(VERSION 3.21)
project(anybind VERSION 0.0.1 LANGUAGES CXX)

include(cmake/anybind.cmake)

option(ANYBIND_BUILD_TESTS "Build the anybind test addons" ${PROJECT_IS_TOP_LEVEL})
if(ANYBIND_BUILD_TESTS)
  add_subdirectory(tests)
endif()
