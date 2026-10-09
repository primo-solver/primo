function(check_profile label phase monotone)
  execute_process(
    COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats --check-model ${ARGN} "${PRIMO_TEST_INPUT}"
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
  )
  if(NOT actual_exit EQUAL 0 OR NOT actual_stdout STREQUAL "sat\n")
    message(FATAL_ERROR "${label}: exit=${actual_exit}\n${actual_stdout}\n${actual_stderr}")
  endif()
  foreach(pair IN ITEMS "lra.model-phase.hints;${phase}"
                        "preprocessing.monotone-elimination.variables;${monotone}")
    list(GET pair 0 statistic)
    list(GET pair 1 expected)
    if(expected)
      if(NOT actual_stderr MATCHES ":${statistic} [1-9][0-9]*(\n|$)")
        message(FATAL_ERROR "${label}: missing active ${statistic}\n${actual_stderr}")
      endif()
    else()
      # The public statistics printer omits zero counters.
      if(actual_stderr MATCHES ":${statistic} [1-9][0-9]*(\n|$)")
        message(FATAL_ERROR "${label}: unexpected active ${statistic}\n${actual_stderr}")
      endif()
    endif()
  endforeach()
endfunction()

check_profile("automatic" TRUE TRUE)
check_profile("explicit both" TRUE TRUE --lra-model-phase --monotone-elimination)
check_profile("explicit opt-outs" FALSE FALSE --no-lra-model-phase --no-monotone-elimination)
check_profile("phase only" TRUE FALSE --no-monotone-elimination)
check_profile("monotone only" FALSE TRUE --no-lra-model-phase)
check_profile("explicit preprocessing profile" FALSE FALSE --preprocessing-mode=off)
check_profile("explicit flags override preprocessing profile" TRUE TRUE
              --preprocessing-mode=off --lra-model-phase --monotone-elimination)
check_profile("false-valued positive flags" FALSE FALSE
              --lra-model-phase=false --monotone-elimination=false)
check_profile("opt-outs take precedence" FALSE FALSE
              --lra-model-phase --no-lra-model-phase
              --monotone-elimination --no-monotone-elimination)
execute_process(COMMAND "${PRIMO_EXECUTABLE}" --help
                RESULT_VARIABLE help_exit OUTPUT_VARIABLE help_stdout)
if(NOT help_exit EQUAL 0)
  message(FATAL_ERROR "help failed: ${help_exit}")
endif()
foreach(option IN ITEMS lra-model-phase monotone-elimination)
  if(NOT help_stdout MATCHES "--${option} +\\[default: auto"
     OR NOT help_stdout MATCHES "--no-${option}")
    message(FATAL_ERROR "missing automatic policy/opt-out in help for ${option}\n${help_stdout}")
  endif()
endforeach()
