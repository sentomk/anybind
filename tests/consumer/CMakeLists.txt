# Mirrors the CMake snippet in README.md, built against the packed npm tarball.
cmake_minimum_required(VERSION 3.21)
project(example CXX)
execute_process(COMMAND node -p "require('anybind').cmakeModule"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE ANYBIND_CMAKE OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
include(${ANYBIND_CMAKE})
anybind_add_module(example example.cpp)
