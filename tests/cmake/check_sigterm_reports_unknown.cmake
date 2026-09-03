# A benchmark harness stops Primo with SIGTERM and reads the last line of
# stdout. Cooperative cancellation alone cannot honour that contract: it is
# observed at tactic boundaries and inside the SAT backend, so a signal that
# lands during parsing, frontend construction, or one preprocessing pass is not
# acted on until that phase ends. Both paths are checked here, because the
# watchdog that bounds the wait cannot flush statistics and must not displace
# the cooperative path that can.

# 1. A signal during a long single-shot phase is answered within the grace
#    period. The generated input is sized so that phase lasts several seconds,
#    which is far longer than the one-second grace.
set(generated_input "${CMAKE_CURRENT_BINARY_DIR}/sigterm_interruptible.smt2")
execute_process(
  COMMAND "${PRIMO_PYTHON}" "${PRIMO_GENERATOR}" "${generated_input}" --assertions 600000
  RESULT_VARIABLE generate_exit
  OUTPUT_VARIABLE generate_stdout
  ERROR_VARIABLE generate_stderr
)
if(NOT generate_exit EQUAL 0)
  message(FATAL_ERROR
    "could not generate the interruptible input: exit ${generate_exit}\n"
    "stdout: ${generate_stdout}\nstderr: ${generate_stderr}")
endif()

# The solver reports its own exit status this way; "timeout" would replace it
# with 124 and hide whether the answer was produced at all.
set(interrupt_script "\
\"$1\" --quiet --cancel-grace-seconds=1 \"$2\" &
solver=$!
sleep 0.15
kill -TERM $solver
wait $solver
")

execute_process(
  COMMAND bash -c "${interrupt_script}" bash "${PRIMO_EXECUTABLE}" "${generated_input}"
  TIMEOUT 3
  RESULT_VARIABLE phase_exit
  OUTPUT_VARIABLE phase_stdout
  ERROR_VARIABLE phase_stderr
)
file(REMOVE "${generated_input}")

if(NOT phase_exit STREQUAL "0")
  message(FATAL_ERROR
    "SIGTERM during a single-shot phase was not answered within the grace period.\n"
    "A non-numeric result below is CMake reporting its own timeout, which is the "
    "regression this guards: the signal is only acted on once the running phase "
    "happens to end.\n"
    "result: ${phase_exit}\nstdout: ${phase_stdout}\nstderr: ${phase_stderr}")
endif()
if(NOT phase_stdout STREQUAL "unknown\n")
  message(FATAL_ERROR
    "SIGTERM must report exactly one unknown answer on stdout\n"
    "stdout: ${phase_stdout}\nstderr: ${phase_stderr}")
endif()

# 2. A signal during SAT solving is still answered cooperatively, which is the
#    only path that can flush in-flight statistics.
set(solve_script "\
\"$1\" --quiet --stats \"$2\" &
solver=$!
sleep 1
kill -TERM $solver
wait $solver
")

execute_process(
  COMMAND bash -c "${solve_script}" bash "${PRIMO_EXECUTABLE}" "${PRIMO_TEST_INPUT}"
  TIMEOUT 30
  RESULT_VARIABLE solve_exit
  OUTPUT_VARIABLE solve_stdout
  ERROR_VARIABLE solve_stderr
)
if(NOT solve_exit STREQUAL "0" OR NOT solve_stdout STREQUAL "unknown\n")
  message(FATAL_ERROR
    "SIGTERM during solving should answer unknown, got result ${solve_exit}\n"
    "stdout: ${solve_stdout}\nstderr: ${solve_stderr}")
endif()
if(NOT solve_stderr MATCHES ":sat.conflicts [1-9]")
  message(FATAL_ERROR
    "cooperative cancellation must still flush live solver statistics\n"
    "stderr: ${solve_stderr}")
endif()

# 3. An uninterrupted run is unaffected, so the watchdog cannot be firing on
#    its own.
execute_process(
  COMMAND "${PRIMO_EXECUTABLE}" --quiet "${PRIMO_SMOKE_INPUT}"
  TIMEOUT 60
  RESULT_VARIABLE normal_exit
  OUTPUT_VARIABLE normal_stdout
  ERROR_VARIABLE normal_stderr
)
if(NOT normal_exit STREQUAL "0" OR NOT normal_stdout STREQUAL "sat\n")
  message(FATAL_ERROR
    "uninterrupted run should still answer sat, got result ${normal_exit}\n"
    "stdout: ${normal_stdout}\nstderr: ${normal_stderr}")
endif()
