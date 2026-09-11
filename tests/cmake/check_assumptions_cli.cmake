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
