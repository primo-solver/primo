execute_process(
  COMMAND "${PRIMO_EXECUTABLE}" --quiet --stats --experimental-split-bools=1
          "${PRIMO_TEST_INPUT}"
  RESULT_VARIABLE split_exit
  OUTPUT_VARIABLE split_stdout
  ERROR_VARIABLE split_stderr
)
if(NOT split_exit EQUAL 0 OR NOT split_stdout STREQUAL "sat\n")
  message(FATAL_ERROR
    "experimental split: expected sat/exit 0, got exit ${split_exit}\n"
    "stdout: ${split_stdout}\nstderr: ${split_stderr}")
endif()
foreach(stat_pattern IN ITEMS
    ":tactic.experimental-split-bools.invocations 1"
    ":experimental.split-bools.selected 1"
    ":experimental.split-bools.output-goals 2")
  if(NOT split_stderr MATCHES "${stat_pattern}")
    message(FATAL_ERROR
      "experimental split: missing statistic '${stat_pattern}'\n${split_stderr}")
  endif()
endforeach()

execute_process(
  COMMAND "${PRIMO_EXECUTABLE}" --quiet --experimental-split-bools=17
          "${PRIMO_TEST_INPUT}"
  RESULT_VARIABLE invalid_exit
  OUTPUT_VARIABLE invalid_stdout
  ERROR_VARIABLE invalid_stderr
)
if(invalid_exit EQUAL 0 OR NOT invalid_stderr MATCHES "not in range")
  message(FATAL_ERROR
    "experimental split bound: expected CLI range rejection, got exit ${invalid_exit}\n"
    "stdout: ${invalid_stdout}\nstderr: ${invalid_stderr}")
endif()

execute_process(
  COMMAND "${PRIMO_EXECUTABLE}" --help
  RESULT_VARIABLE help_exit
  OUTPUT_VARIABLE help_stdout
  ERROR_VARIABLE help_stderr
)
if(NOT help_exit EQUAL 0 OR
   NOT help_stdout MATCHES "Experimental tactic options:" OR
   NOT help_stdout MATCHES "--experimental-split-bools[^\n]*\\[0\\]" OR
   NOT help_stdout MATCHES "EXPERIMENTAL: split on up to k Boolean atoms")
  message(FATAL_ERROR
    "experimental split help is missing its status/default\n"
    "stdout: ${help_stdout}\nstderr: ${help_stderr}")
endif()
