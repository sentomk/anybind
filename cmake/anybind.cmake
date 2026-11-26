# anybind CMake integration.
#
#   include(path/to/anybind/cmake/anybind.cmake)   # or: node -p "require('anybind').cmakeModule"
#   anybind_add_module(my_addon src/my_addon.cpp)
#
# Node-API headers are taken from ANYBIND_NODE_API_INCLUDE_DIR if set, otherwise from the
# `include/node` directory of the `node` executable found on PATH.

include_guard(GLOBAL)

get_filename_component(ANYBIND_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT ANYBIND_NODE_API_INCLUDE_DIR)
  find_program(ANYBIND_NODE_EXECUTABLE NAMES node REQUIRED)
  execute_process(
    COMMAND "${ANYBIND_NODE_EXECUTABLE}" -p
            "require('path').resolve(process.execPath, '..', '..', 'include', 'node')"
    OUTPUT_VARIABLE _anybind_node_include
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  set(ANYBIND_NODE_API_INCLUDE_DIR "${_anybind_node_include}"
      CACHE PATH "Directory containing node_api.h")
endif()

if(NOT EXISTS "${ANYBIND_NODE_API_INCLUDE_DIR}/node_api.h")
  message(FATAL_ERROR
    "anybind: node_api.h not found in '${ANYBIND_NODE_API_INCLUDE_DIR}'. "
    "Set ANYBIND_NODE_API_INCLUDE_DIR to a directory containing the Node-API headers.")
endif()

if(NOT TARGET anybind::anybind)
  add_library(anybind_headers INTERFACE)
  add_library(anybind::anybind ALIAS anybind_headers)
  target_include_directories(anybind_headers INTERFACE
    "${ANYBIND_ROOT}/include"
    "${ANYBIND_NODE_API_INCLUDE_DIR}")
  target_compile_features(anybind_headers INTERFACE cxx_std_17)
endif()

# anybind_add_module(<name> <sources>...)
# Builds <name>.node, loadable by any runtime that implements Node-API.
function(anybind_add_module name)
  add_library(${name} MODULE ${ARGN})
  target_link_libraries(${name} PRIVATE anybind::anybind)
  set_target_properties(${name} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(APPLE)
    # Node-API symbols are resolved from the host process at load time.
    target_link_options(${name} PRIVATE -undefined dynamic_lookup)
  elseif(WIN32)
    message(WARNING "anybind: Windows builds (linking against node.lib) are not supported yet.")
  endif()
endfunction()
