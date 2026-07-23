cmake_minimum_required(VERSION 3.20)

project(primo LANGUAGES CXX)

# Default to a Release build when the user (or an IDE/script) doesn't ask
# for a specific one: an unset CMAKE_BUILD_TYPE is unoptimized with no
# NDEBUG, and gives no indication in the binary that that's what happened
# (see the version banner's build-type field) -- easy to mistake for a
# real performance regression instead of a plain unconfigured build.
# Multi-config generators (Ninja Multi-Config, Visual Studio, Xcode) select
# configuration at build time instead, so this only applies when there
# isn't one.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRIMO_WARNINGS_AS_ERRORS "Treat Primo C++ compiler warnings as errors" ON)
set(PRIMO_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizer list for Primo targets, e.g. address;undefined")
set(PRIMO_CADICAL_ROOT "" CACHE PATH "CaDiCaL installation prefix to prefer over contrib/local")

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

set(PRIMO_CADICAL_INCLUDE_HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include")
set(PRIMO_CADICAL_LIBRARY_HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib")
if(PRIMO_CADICAL_ROOT)
  list(PREPEND PRIMO_CADICAL_INCLUDE_HINTS "${PRIMO_CADICAL_ROOT}/include")
  list(PREPEND PRIMO_CADICAL_LIBRARY_HINTS "${PRIMO_CADICAL_ROOT}/lib")
endif()
find_path(IPASIR_UP_INCLUDE_DIR
  NAMES ipasir_up.h ipasir.h
  HINTS ${PRIMO_CADICAL_INCLUDE_HINTS}
)
find_library(CADICAL_LIBRARY
  NAMES cadical
  HINTS ${PRIMO_CADICAL_LIBRARY_HINTS}
)

if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  message(STATUS "Found IPASIR/CaDiCaL headers and library")
  message(STATUS "  IPASIR headers: ${IPASIR_UP_INCLUDE_DIR}")
  message(STATUS "  CaDiCaL library: ${CADICAL_LIBRARY}")
  set(CMAKE_REQUIRED_INCLUDES "${IPASIR_UP_INCLUDE_DIR}")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     int main() { auto fn = &CaDiCaL::Solver::resize; (void) fn; return 0; }"
    PRIMO_CADICAL_HAS_RESIZE
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     int main() { auto fn = &CaDiCaL::Solver::reserve; (void) fn; return 0; }"
    PRIMO_CADICAL_HAS_RESERVE
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     struct P : CaDiCaL::ExternalPropagator {
       void notify_assignment(const std::vector<int>&) override {}
       void notify_new_decision_level() override {}
       void notify_backtrack(size_t) override {}
       bool cb_check_found_model(const std::vector<int>&) override { return true; }
       bool cb_has_external_clause(bool&) override { return false; }
       int cb_add_external_clause_lit() override { return 0; }
     };
     int main() { P p; (void)p; }"
    PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR
  )
  check_cxx_source_compiles(
    "#include <cadical.hpp>
     struct P : CaDiCaL::ExternalPropagator {
       void notify_assignment(int, bool) override {}
       void notify_new_decision_level() override {}
       void notify_backtrack(size_t) override {}
       bool cb_check_found_model(const std::vector<int>&) override { return true; }
       bool cb_has_external_clause() override { return false; }
       int cb_add_external_clause_lit() override { return 0; }
     };
     int main() { P p; (void)p; }"
    PRIMO_CADICAL_EXTERNAL_NOTIFY_SCALAR
  )
  unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  unset(CMAKE_REQUIRED_INCLUDES)
else()
  message(STATUS "IPASIR-UP/CaDiCaL not found; SAT backend remains a scaffold")
endif()

find_path(GMP_INCLUDE_DIR
  NAMES gmp.h
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
find_path(GMPXX_INCLUDE_DIR
  NAMES gmpxx.h
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/include"
)
find_library(GMPXX_LIBRARY
  NAMES gmpxx
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib"
)
find_library(GMP_LIBRARY
  NAMES gmp
  HINTS "${CMAKE_SOURCE_DIR}/contrib/local/lib"
)
if(GMP_INCLUDE_DIR AND GMPXX_INCLUDE_DIR AND GMP_LIBRARY AND GMPXX_LIBRARY)
  message(STATUS "Found GMP: ${GMP_LIBRARY}")
else()
  message(FATAL_ERROR "GMP (with C++ wrapper) not found. Run `make contrib-gmp` or install GMP so gmp.h/gmpxx.h are available.")
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
target_include_directories(primo_core SYSTEM PUBLIC "${GMP_INCLUDE_DIR}" "${GMPXX_INCLUDE_DIR}")
target_link_libraries(primo_core PUBLIC "${GMPXX_LIBRARY}" "${GMP_LIBRARY}")
if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  target_compile_definitions(primo_core PUBLIC PRIMO_HAVE_IPASIR=1)
  target_compile_definitions(primo_core PUBLIC
    PRIMO_CADICAL_HAS_RESIZE=$<BOOL:${PRIMO_CADICAL_HAS_RESIZE}>
    PRIMO_CADICAL_HAS_RESERVE=$<BOOL:${PRIMO_CADICAL_HAS_RESERVE}>
    PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR=$<BOOL:${PRIMO_CADICAL_EXTERNAL_NOTIFY_VECTOR}>
    PRIMO_CADICAL_EXTERNAL_NOTIFY_SCALAR=$<BOOL:${PRIMO_CADICAL_EXTERNAL_NOTIFY_SCALAR}>
  )
  target_include_directories(primo_core SYSTEM PUBLIC "${IPASIR_UP_INCLUDE_DIR}")
  target_link_libraries(primo_core PUBLIC "${CADICAL_LIBRARY}")
else()
  target_compile_definitions(primo_core PUBLIC PRIMO_HAVE_IPASIR=0)
endif()

add_executable(primo src/main.cpp)
if(PRIMO_SANITIZERS)
  list(JOIN PRIMO_SANITIZERS "," primo_sanitizers_joined)
else()
  set(primo_sanitizers_joined "none")
endif()
target_compile_definitions(primo PRIVATE
  "PRIMO_BUILD_TYPE=\"$<IF:$<CONFIG:>,unspecified,$<CONFIG>>\""
  "PRIMO_SANITIZERS_BANNER=\"${primo_sanitizers_joined}\""
)
add_custom_target(primo_git_revision_header ALL
  COMMAND "${CMAKE_COMMAND}"
    -DPRIMO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DPRIMO_GIT_REVISION_HEADER=${CMAKE_BINARY_DIR}/generated/primo_git_revision.hpp
    -P ${CMAKE_SOURCE_DIR}/scripts/write_git_revision.cmake
  BYPRODUCTS "${CMAKE_BINARY_DIR}/generated/primo_git_revision.hpp"
  VERBATIM
)
add_dependencies(primo primo_git_revision_header)
primo_enable_warnings(primo)
primo_enable_sanitizers(primo)
target_link_libraries(primo PRIVATE primo_core)
target_include_directories(primo PRIVATE src)
target_include_directories(primo PRIVATE "${CMAKE_BINARY_DIR}")
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

add_executable(primo_rational_tests tests/unit/rational_tests.cpp)
primo_enable_warnings(primo_rational_tests)
primo_enable_sanitizers(primo_rational_tests)
target_link_libraries(primo_rational_tests PRIVATE primo_core)
add_test(NAME primo_rational_tests COMMAND primo_rational_tests)

add_executable(primo_qf_lra_solver_tests tests/unit/qf_lra_solver_tests.cpp)
primo_enable_warnings(primo_qf_lra_solver_tests)
primo_enable_sanitizers(primo_qf_lra_solver_tests)
target_link_libraries(primo_qf_lra_solver_tests PRIVATE primo_core)
add_test(NAME primo_qf_lra_solver_tests COMMAND primo_qf_lra_solver_tests)

add_executable(primo_linear_polynomial_tests tests/unit/linear_polynomial_tests.cpp)
primo_enable_warnings(primo_linear_polynomial_tests)
primo_enable_sanitizers(primo_linear_polynomial_tests)
target_link_libraries(primo_linear_polynomial_tests PRIVATE primo_core)
add_test(NAME primo_linear_polynomial_tests COMMAND primo_linear_polynomial_tests)

add_executable(primo_lra_atom_tests tests/unit/lra_atom_tests.cpp)
primo_enable_warnings(primo_lra_atom_tests)
primo_enable_sanitizers(primo_lra_atom_tests)
target_link_libraries(primo_lra_atom_tests PRIVATE primo_core)
add_test(NAME primo_lra_atom_tests COMMAND primo_lra_atom_tests)

add_executable(primo_lra_tableau_tests tests/unit/lra_tableau_tests.cpp)
primo_enable_warnings(primo_lra_tableau_tests)
primo_enable_sanitizers(primo_lra_tableau_tests)
target_link_libraries(primo_lra_tableau_tests PRIVATE primo_core)
add_test(NAME primo_lra_tableau_tests COMMAND primo_lra_tableau_tests)
