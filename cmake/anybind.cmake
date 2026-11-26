# anybind CMake integration.
#
#   include(path/to/anybind/cmake/anybind.cmake)   # or: node -p "require('anybind').cmakeModule"
#   anybind_add_module(my_addon src/my_addon.cpp)
#
# Node-API headers come from ANYBIND_NODE_API_INCLUDE_DIR if set, otherwise from the
# `node-api-headers` package that anybind depends on, otherwise from the `include/node`
# directory next to the `node` executable found on PATH.
#
# On Windows (MSVC or clang-cl), an import library is generated from ANYBIND_NODE_API_DEF
# (defaulting to the `.def` file shipped with `node-api-headers`), and the host binary is
# delay-loaded so that the same addon loads in node.exe, electron.exe, bun.exe or any other
# process that exports Node-API.

include_guard(GLOBAL)

get_filename_component(ANYBIND_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(_anybind_need_def OFF)
if(WIN32 AND NOT ANYBIND_NODE_API_DEF)
  set(_anybind_need_def ON)
endif()

if(NOT ANYBIND_NODE_API_INCLUDE_DIR OR _anybind_need_def)
  find_program(ANYBIND_NODE_EXECUTABLE NAMES node REQUIRED)
  # Prints "<include dir>\n<def path>" from node-api-headers, or an empty string.
  execute_process(
    COMMAND "${ANYBIND_NODE_EXECUTABLE}" -e
            "try { const h = require(require.resolve('node-api-headers', { paths: [process.argv[1]] })); console.log(h.include_dir + '\\n' + h.def_paths.node_api_def); } catch {}"
            "${ANYBIND_ROOT}"
    OUTPUT_VARIABLE _anybind_headers
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  if(_anybind_headers)
    string(REPLACE "\n" ";" _anybind_headers "${_anybind_headers}")
    list(GET _anybind_headers 0 _anybind_include)
    list(GET _anybind_headers 1 _anybind_def)
    file(TO_CMAKE_PATH "${_anybind_include}" _anybind_include)
    file(TO_CMAKE_PATH "${_anybind_def}" _anybind_def)
  else()
    execute_process(
      COMMAND "${ANYBIND_NODE_EXECUTABLE}" -p
              "require('path').resolve(process.execPath, '..', '..', 'include', 'node')"
      OUTPUT_VARIABLE _anybind_include
      OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY)
    file(TO_CMAKE_PATH "${_anybind_include}" _anybind_include)
    set(_anybind_def "")
  endif()
  if(NOT ANYBIND_NODE_API_INCLUDE_DIR)
    set(ANYBIND_NODE_API_INCLUDE_DIR "${_anybind_include}"
        CACHE PATH "Directory containing node_api.h")
  endif()
  if(_anybind_need_def AND _anybind_def)
    set(ANYBIND_NODE_API_DEF "${_anybind_def}"
        CACHE FILEPATH "Module-definition file listing the Node-API exports (Windows)")
  endif()
endif()

if(NOT EXISTS "${ANYBIND_NODE_API_INCLUDE_DIR}/node_api.h")
  message(FATAL_ERROR
    "anybind: node_api.h not found in '${ANYBIND_NODE_API_INCLUDE_DIR}'. "
    "Install the `node-api-headers` package or set ANYBIND_NODE_API_INCLUDE_DIR to a "
    "directory containing the Node-API headers.")
endif()

if(NOT TARGET anybind::anybind)
  add_library(anybind_headers INTERFACE)
  add_library(anybind::anybind ALIAS anybind_headers)
  target_include_directories(anybind_headers INTERFACE
    "${ANYBIND_ROOT}/include"
    "${ANYBIND_NODE_API_INCLUDE_DIR}")
  target_compile_features(anybind_headers INTERFACE cxx_std_17)

  if(WIN32)
    if(NOT MSVC)
      message(FATAL_ERROR "anybind: on Windows only MSVC and clang-cl are supported.")
    endif()
    if(NOT ANYBIND_NODE_API_DEF OR NOT EXISTS "${ANYBIND_NODE_API_DEF}")
      message(FATAL_ERROR
        "anybind: Node-API .def file not found. Install the `node-api-headers` package or "
        "set ANYBIND_NODE_API_DEF.")
    endif()

    # Import library for the Node-API symbols, all attributed to node.exe. The
    # delay-load hook redirects them to whichever process actually loads the addon.
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64)$")
      set(_anybind_machine X64)
    elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(ARM64|arm64)$")
      set(_anybind_machine ARM64)
    elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86)$")
      set(_anybind_machine X86)
    else()
      message(FATAL_ERROR
        "anybind: unsupported Windows architecture '${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}'.")
    endif()
    set(_anybind_import_lib "${CMAKE_BINARY_DIR}/anybind/node_api.lib")
    add_custom_command(
      OUTPUT "${_anybind_import_lib}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/anybind"
      COMMAND "${CMAKE_AR}" /nologo "/def:${ANYBIND_NODE_API_DEF}"
              "/out:${_anybind_import_lib}" "/machine:${_anybind_machine}"
      DEPENDS "${ANYBIND_NODE_API_DEF}"
      COMMENT "anybind: generating Node-API import library"
      VERBATIM)
    add_custom_target(anybind_node_api_lib DEPENDS "${_anybind_import_lib}")
    add_dependencies(anybind_headers anybind_node_api_lib)

    target_link_libraries(anybind_headers INTERFACE "${_anybind_import_lib}" delayimp)
    target_link_options(anybind_headers INTERFACE "/DELAYLOAD:node.exe")
  endif()
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
    target_sources(${name} PRIVATE "${ANYBIND_ROOT}/cmake/win_delay_load_hook.cc")
  endif()
endfunction()
