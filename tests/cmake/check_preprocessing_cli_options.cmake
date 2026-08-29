function(run_primo_case label expected_exit expected_stderr)
  execute_process(
    COMMAND "${PRIMO_EXECUTABLE}" --quiet ${ARGN} "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
  )
  if(NOT actual_exit EQUAL expected_exit)
    message(FATAL_ERROR
      "${label}: expected exit ${expected_exit}, got ${actual_exit}\n"
      "stdout: ${actual_stdout}\nstderr: ${actual_stderr}")
  endif()
  if(expected_exit EQUAL 0 AND NOT actual_stdout STREQUAL "sat\n")
    message(FATAL_ERROR "${label}: expected sat, got: ${actual_stdout}")
  endif()
  if(expected_stderr STREQUAL "")
    if(NOT actual_stderr STREQUAL "")
      message(FATAL_ERROR "${label}: unexpected stderr: ${actual_stderr}")
    endif()
  elseif(NOT actual_stderr MATCHES "${expected_stderr}")
    message(FATAL_ERROR
      "${label}: stderr did not match '${expected_stderr}': ${actual_stderr}")
  endif()
endfunction()

run_primo_case("canonical preprocessing mode" 0 "" --preprocessing-mode=off)
run_primo_case("canonical auto selector" 0 "" --auto-preprocess-selector=structural)
run_primo_case("arithmetic atom canonicalization" 0 "" --arithmetic-atom-canonicalization)

execute_process(
  COMMAND "${PRIMO_EXECUTABLE}" --help
  RESULT_VARIABLE help_exit
  OUTPUT_VARIABLE help_stdout
  ERROR_VARIABLE help_stderr
)
if(NOT help_exit EQUAL 0)
  message(FATAL_ERROR
    "help defaults: expected exit 0, got ${help_exit}\n"
    "stdout: ${help_stdout}\nstderr: ${help_stderr}")
endif()
if(help_stdout MATCHES "--qf-uf-preprocess" OR help_stdout MATCHES "--qf-uf-auto-selector")
  message(FATAL_ERROR "removed preprocessing aliases still appear in --help\n${help_stdout}")
endif()

foreach(option_pattern IN ITEMS
    "--auto-basic-multi \\[default: (on|off)\\]"
    "--preprocessing-mode[^\n]*\\[[^]]+\\]"
    "--preprocessing-passes[^\n]*\\[[^]]+\\]"
    "--preprocessing-adaptive-extra-passes[^\n]*\\[[^]]+\\]"
    "--preprocessing-adaptive-shrink-percent[^\n]*\\[[^]]+\\]")
  if(NOT help_stdout MATCHES "${option_pattern}")
    message(FATAL_ERROR
      "help defaults: no generated default matched '${option_pattern}'\n${help_stdout}")
  endif()
endforeach()
