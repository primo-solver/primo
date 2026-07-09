cmake_minimum_required(VERSION 3.20)

project(primo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRIMO_WARNINGS_AS_ERRORS "Treat Primo C++ compiler warnings as errors" ON)
set(PRIMO_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizer list for Primo targets, e.g. address;undefined")

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/contrib/local")

function(primo_enable_sanitizers target)
  if(NOT PRIMO_SANITIZERS)
    return()
  endif()
  if(MSVC)
    if("address" IN_LIST PRIMO_SANITIZERS)
      target_compile_options(${target} PRIVATE /fsanitize=address)
      target_link_options(${target} PRIVATE /fsanitize=address)
    else()
      message(FATAL_ERROR "MSVC sanitizer support is limited to PRIMO_SANITIZERS=address")
    endif()
    return()
  endif()

  list(JOIN PRIMO_SANITIZERS "," sanitizer_flags)
  target_compile_options(${target} PRIVATE -fsanitize=${sanitizer_flags} -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=${sanitizer_flags})
endfunction()

function(primo_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(PRIMO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
      -Wshadow
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Woverloaded-virtual
      -Wnull-dereference
      -Wdouble-promotion
      -Wformat=2
      -Wimplicit-fallthrough
    )
    if(PRIMO_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

find_path(CLI11_INCLUDE_DIR
  NAMES CLI/CLI.hpp
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
if(CLI11_INCLUDE_DIR)
  message(STATUS "Found CLI11: ${CLI11_INCLUDE_DIR}")
else()
  message(FATAL_ERROR "CLI11 not found. Run `make contrib-required` or install CLI11 so CLI/CLI.hpp is available.")
endif()

find_path(ANTLR4_RUNTIME_INCLUDE_DIR
  NAMES antlr4-runtime.h
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include/antlr4-runtime"
)
find_library(ANTLR4_RUNTIME_LIBRARY
  NAMES antlr4-runtime
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib"
)
if(ANTLR4_RUNTIME_INCLUDE_DIR AND ANTLR4_RUNTIME_LIBRARY)
  message(STATUS "Found ANTLR4 runtime: ${ANTLR4_RUNTIME_LIBRARY}")
else()
  message(FATAL_ERROR "ANTLR4 C++ runtime not found. Run `make contrib-required`.")
endif()

find_path(IPASIR_UP_INCLUDE_DIR
  NAMES ipasir_up.h ipasir.h
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
find_library(CADICAL_LIBRARY
  NAMES cadical
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib"
)

if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  message(STATUS "Found IPASIR/CaDiCaL headers and library")
  message(STATUS "  IPASIR headers: ${IPASIR_UP_INCLUDE_DIR}")
  message(STATUS "  CaDiCaL library: ${CADICAL_LIBRARY}")
else()
  message(STATUS "IPASIR-UP/CaDiCaL not found; SAT backend remains a scaffold")
endif()

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE PRIMO_GIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT PRIMO_GIT_REVISION)
  set(PRIMO_GIT_REVISION "unknown")
endif()

file(GLOB_RECURSE PRIMO_LIBRARY_SOURCES CONFIGURE_DEPENDS
  src/cli/*.cpp
  src/core/*.cpp
  src/sat/*.cpp
  src/smt/*.cpp
  src/smtlib/*.cpp
  src/theory/*.cpp
  src/util/*.cpp
)
file(GLOB_RECURSE PRIMO_GENERATED_SOURCES CONFIGURE_DEPENDS
  src/smtlib/antlr/*.cpp
)
if(PRIMO_GENERATED_SOURCES)
  if(MSVC)
    set_source_files_properties(${PRIMO_GENERATED_SOURCES} PROPERTIES COMPILE_OPTIONS /w)
  else()
    set_source_files_properties(${PRIMO_GENERATED_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
  endif()
endif()

add_library(primo_core ${PRIMO_LIBRARY_SOURCES})
primo_enable_warnings(primo_core)
primo_enable_sanitizers(primo_core)
target_include_directories(primo_core PUBLIC src)
target_include_directories(primo_core SYSTEM PUBLIC "${ANTLR4_RUNTIME_INCLUDE_DIR}")
target_compile_definitions(primo_core PUBLIC ANTLR4CPP_STATIC=1)
target_link_libraries(primo_core PUBLIC "${ANTLR4_RUNTIME_LIBRARY}")
if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  target_compile_definitions(primo_core PUBLIC PRIMO_HAVE_IPASIR=1)
  target_include_directories(primo_core SYSTEM PUBLIC "${IPASIR_UP_INCLUDE_DIR}")
  target_link_libraries(primo_core PUBLIC "${CADICAL_LIBRARY}")
else()
  target_compile_definitions(primo_core PUBLIC PRIMO_HAVE_IPASIR=0)
endif()

add_executable(primo src/main.cpp)
primo_enable_warnings(primo)
primo_enable_sanitizers(primo)
target_link_libraries(primo PRIVATE primo_core)
target_compile_definitions(primo PRIVATE PRIMO_GIT_REVISION="${PRIMO_GIT_REVISION}")
target_include_directories(primo PRIVATE src)
target_include_directories(primo SYSTEM PRIVATE "${CLI11_INCLUDE_DIR}")

enable_testing()
add_test(NAME primo_version COMMAND primo --version)

add_executable(primo_core_tests tests/unit/core_tests.cpp)
primo_enable_warnings(primo_core_tests)
primo_enable_sanitizers(primo_core_tests)
target_link_libraries(primo_core_tests PRIVATE primo_core)
add_test(NAME primo_core_tests COMMAND primo_core_tests)

add_executable(primo_smtlib_parser_tests tests/unit/smtlib_parser_tests.cpp)
primo_enable_warnings(primo_smtlib_parser_tests)
primo_enable_sanitizers(primo_smtlib_parser_tests)
target_link_libraries(primo_smtlib_parser_tests PRIVATE primo_core)
add_test(NAME primo_smtlib_parser_tests COMMAND primo_smtlib_parser_tests)

add_executable(primo_sat_backend_tests tests/unit/sat_backend_tests.cpp)
primo_enable_warnings(primo_sat_backend_tests)
primo_enable_sanitizers(primo_sat_backend_tests)
target_link_libraries(primo_sat_backend_tests PRIVATE primo_core)
add_test(NAME primo_sat_backend_tests COMMAND primo_sat_backend_tests)

add_executable(primo_boolean_solver_tests tests/unit/boolean_solver_tests.cpp)
primo_enable_warnings(primo_boolean_solver_tests)
primo_enable_sanitizers(primo_boolean_solver_tests)
target_link_libraries(primo_boolean_solver_tests PRIVATE primo_core)
add_test(NAME primo_boolean_solver_tests COMMAND primo_boolean_solver_tests)

add_executable(primo_euf_solver_tests tests/unit/euf_solver_tests.cpp)
primo_enable_warnings(primo_euf_solver_tests)
primo_enable_sanitizers(primo_euf_solver_tests)
target_link_libraries(primo_euf_solver_tests PRIVATE primo_core)
add_test(NAME primo_euf_solver_tests COMMAND primo_euf_solver_tests)

add_executable(primo_preprocessing_tests tests/unit/preprocessing_tests.cpp)
primo_enable_warnings(primo_preprocessing_tests)
primo_enable_sanitizers(primo_preprocessing_tests)
target_link_libraries(primo_preprocessing_tests PRIVATE primo_core)
add_test(NAME primo_preprocessing_tests COMMAND primo_preprocessing_tests)
