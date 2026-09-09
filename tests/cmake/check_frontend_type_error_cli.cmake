execute_process(
  COMMAND "${PRIMO_EXECUTABLE}" --quiet "${PRIMO_TEST_INPUT}"
  RESULT_VARIABLE actual_exit
  OUTPUT_VARIABLE actual_stdout
  ERROR_VARIABLE actual_stderr
)
if(NOT actual_exit EQUAL 2 OR NOT actual_stdout MATCHES "^\\(error \"" OR
   NOT actual_stderr MATCHES "expected Real or Int term")
  message(FATAL_ERROR
    "frontend type error: expected SMT-LIB error/exit 2, got exit ${actual_exit}\n"
    "stdout: ${actual_stdout}\nstderr: ${actual_stderr}")
endif()
