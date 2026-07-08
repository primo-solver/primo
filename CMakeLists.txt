cmake_minimum_required(VERSION 3.20)

project(primo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/contrib/local")

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

file(GLOB_RECURSE PRIMO_LIBRARY_SOURCES CONFIGURE_DEPENDS
  src/cli/*.cpp
  src/core/*.cpp
  src/sat/*.cpp
  src/smt/*.cpp
  src/smtlib/*.cpp
  src/theory/*.cpp
  src/util/*.cpp
)

add_library(primo_core ${PRIMO_LIBRARY_SOURCES})
target_include_directories(primo_core PUBLIC src)
if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  target_compile_definitions(primo_core PUBLIC PRIMO_HAVE_IPASIR=1)
  target_include_directories(primo_core PUBLIC "${IPASIR_UP_INCLUDE_DIR}")
  target_link_libraries(primo_core PUBLIC "${CADICAL_LIBRARY}")
else()
  target_compile_definitions(primo_core PUBLIC PRIMO_HAVE_IPASIR=0)
endif()

add_executable(primo src/main.cpp)
target_link_libraries(primo PRIVATE primo_core)

target_include_directories(primo PRIVATE src)

enable_testing()
add_test(NAME primo_version COMMAND primo --version)

add_executable(primo_core_tests tests/unit/core_tests.cpp)
target_link_libraries(primo_core_tests PRIVATE primo_core)
add_test(NAME primo_core_tests COMMAND primo_core_tests)

add_executable(primo_smtlib_parser_tests tests/unit/smtlib_parser_tests.cpp)
target_link_libraries(primo_smtlib_parser_tests PRIVATE primo_core)
add_test(NAME primo_smtlib_parser_tests COMMAND primo_smtlib_parser_tests)

add_executable(primo_sat_backend_tests tests/unit/sat_backend_tests.cpp)
target_link_libraries(primo_sat_backend_tests PRIVATE primo_core)
add_test(NAME primo_sat_backend_tests COMMAND primo_sat_backend_tests)

add_executable(primo_boolean_solver_tests tests/unit/boolean_solver_tests.cpp)
target_link_libraries(primo_boolean_solver_tests PRIVATE primo_core)
add_test(NAME primo_boolean_solver_tests COMMAND primo_boolean_solver_tests)
