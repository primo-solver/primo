foreach(mode rebuild assumptions)
  execute_process(
    COMMAND "${PRIMO}" --quiet "--incremental-mode=${mode}"
    INPUT_FILE "${INPUT}"
    OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result
    TIMEOUT 30)
  if(NOT result EQUAL 0 OR NOT output STREQUAL "unsat\nsat\nsat\nunsat\nsat\n")
    message(FATAL_ERROR "${mode}: ${result}: ${output}: ${error}")
  endif()
endforeach()

# Exercise the exact explicit settings used by the UF SLURM option matrix.
if(INPUT MATCHES "assumption_uf_scopes")
  foreach(preprocessing auto off)
    foreach(propagation auto online conflict)
      execute_process(
        COMMAND "${PRIMO}" --quiet --incremental-mode=assumptions
          "--preprocessing-mode=${preprocessing}" "--euf-prop=${propagation}"
        INPUT_FILE "${INPUT}"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result
        TIMEOUT 30)
      if(NOT result EQUAL 0 OR NOT output STREQUAL "unsat\nsat\nsat\nunsat\nsat\n")
        message(FATAL_ERROR "${preprocessing}/${propagation}: ${result}: ${output}: ${error}")
      endif()
    endforeach()
  endforeach()
endif()
