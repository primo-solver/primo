cmake_minimum_required(VERSION 3.20)

project(primo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(IPASIR_UP_INCLUDE_DIR NAMES ipasir_up.h ipasir.h)
find_library(CADICAL_LIBRARY NAMES cadical)

if(IPASIR_UP_INCLUDE_DIR AND CADICAL_LIBRARY)
  message(STATUS "Found IPASIR/CaDiCaL headers and library")
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

add_executable(primo src/main.cpp)
target_link_libraries(primo PRIVATE primo_core)

target_include_directories(primo PRIVATE src)

enable_testing()
add_test(NAME primo_version COMMAND primo --version)
