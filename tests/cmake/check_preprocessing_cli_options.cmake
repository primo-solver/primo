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
run_primo_case("deprecated preprocessing mode" 0 "--qf-uf-preprocess is deprecated"
               --qf-uf-preprocess=off)
run_primo_case("deprecated auto selector" 0 "--qf-uf-auto-selector is deprecated"
               --qf-uf-auto-selector=structural)
run_primo_case("mixed preprocessing mode names" 2 "cannot be used together"
               --preprocessing-mode=off --qf-uf-preprocess=off)
run_primo_case("mixed auto selector names" 2 "cannot be used together"
               --auto-preprocess-selector=legacy --qf-uf-auto-selector=legacy)

function(compare_alias label canonical_option deprecated_option warning)
  execute_process(
    COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats "${canonical_option}" "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE canonical_exit
    OUTPUT_VARIABLE canonical_stdout
    ERROR_VARIABLE canonical_stderr
  )
  execute_process(
    COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats "${deprecated_option}" "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE deprecated_exit
    OUTPUT_VARIABLE deprecated_stdout
    ERROR_VARIABLE deprecated_stderr
  )
  string(REPLACE "${warning}\n" "" deprecated_stats "${deprecated_stderr}")
  string(REGEX REPLACE ":time\\.[^\n]*\n" "" canonical_stats "${canonical_stderr}")
  string(REGEX REPLACE ":time\\.[^\n]*\n" "" deprecated_stats "${deprecated_stats}")
  if(NOT canonical_exit EQUAL 0 OR NOT deprecated_exit EQUAL 0 OR
     NOT canonical_stdout STREQUAL deprecated_stdout OR
     NOT canonical_stats STREQUAL deprecated_stats)
    message(FATAL_ERROR
      "${label}: canonical and deprecated options differ\n"
      "canonical stdout: ${canonical_stdout}\ndeprecated stdout: ${deprecated_stdout}\n"
      "canonical stderr: ${canonical_stderr}\ndeprecated stderr: ${deprecated_stderr}")
  endif()
endfunction()

compare_alias("preprocessing mode alias" "--preprocessing-mode=off" "--qf-uf-preprocess=off"
              "warning: --qf-uf-preprocess is deprecated; use --preprocessing-mode")
compare_alias("auto selector alias" "--auto-preprocess-selector=structural"
              "--qf-uf-auto-selector=structural"
              "warning: --qf-uf-auto-selector is deprecated; use --auto-preprocess-selector")
