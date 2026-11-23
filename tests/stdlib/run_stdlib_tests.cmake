if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}/interpreter")
set(terminal_file_work "${WORK_DIR}/terminal-input-zfile")
file(MAKE_DIRECTORY "${terminal_file_work}")
set(terminal_file_input "${WORK_DIR}/terminal-input-file.stdin")
file(WRITE "${terminal_file_input}" "add\nRemember to test\n")
execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/stdlib/terminal_input_with_zfile.zp"
    WORKING_DIRECTORY "${terminal_file_work}"
    INPUT_FILE "${terminal_file_input}"
    RESULT_VARIABLE terminal_file_result
    OUTPUT_VARIABLE terminal_file_output
    ERROR_VARIABLE terminal_file_error
    TIMEOUT 20
)
if(NOT terminal_file_result EQUAL 0 OR
   NOT "${terminal_file_output}" STREQUAL "Remember to test\n")
    message(FATAL_ERROR
        "terminal input with zfile failed (${terminal_file_result})\n"
        "${terminal_file_output}${terminal_file_error}")
endif()

set(source "${SOURCE_DIR}/tests/stdlib/smoke.zp")
set(expected "sqrt-ok\npow-ok\nabs-ok\nmin-max-ok\nrounding-ok\ntrig-ok\ninverse-trig-ok\nlog-exp-ok\nALICE\nalpha,beta\nbcd\nxxx\nlower-ok\nreplace-ok\nprefix-suffix-ok\nstring-reverse-ok\nstring-length-ok\nrepeat-zero-ok\nsubstring-end-ok\nunicode-length-ok\nЖ\n🙂Жé\nÉЖ\néж\né\né🙂Ж\néя\nreverse-ok\nsort-ok\ncollections-copy-ok\nsum-ok\naverage-ok\ncollection-min-max-ok\nunique-ok\ncollection-search-ok\nfirst-last-ok\nempty-ok\nset-unique-ok\nset-remove-ok\nmap-update-ok\nrandom-ok\nrandom-seed-ok\nchoice-ok\nrandom-boolean-ok\nshuffle-ok\nreport.txt\nnormalize-ok\nextension-ok\ndirectory-ok\nabsolute-ok\nvisible\nenv-exists\ntime-ok\ndate-parts-ok\nclock-parts-ok\nname=alice\nfile-exists-ok\nfile-size-ok\nfile-append-ok\nfile-lines-write-ok\nfile-lines-read-ok\nfile-lines-final-line-ok\nfile-lines-crlf-ok\nfile-lines-empty-ok\nfile-lines-delete-ok\ndirectory-list-ok\nnested-dir-ok\nroot-dir-ok\nargs-count-ok\nargs-has-ok\nargs-all-ok\nenv-args-ok\nhome-dir-ok\ncurrent-dir-ok\nstdlib-gc-string-ok\nstdlib-gc-array-ok\nlog-ok\nfile-delete-ok\n")

execute_process(
    COMMAND "${ZP}" run "${source}" -- "${WORK_DIR}/interpreter" "test-flag"
    RESULT_VARIABLE interpreter_result
    OUTPUT_VARIABLE interpreter_output
    ERROR_VARIABLE interpreter_error
    TIMEOUT 20
)
if(NOT interpreter_result EQUAL 0 OR NOT "${interpreter_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "standard-library interpreter test failed (${interpreter_result})\n${interpreter_output}${interpreter_error}")
endif()
if(NOT EXISTS "${WORK_DIR}/interpreter/run.log")
    message(FATAL_ERROR "standard-library logger did not create its output file")
endif()
file(READ "${WORK_DIR}/interpreter/run.log" interpreter_log)
if(NOT interpreter_log MATCHES "warning: visible" OR interpreter_log MATCHES "filtered")
    message(FATAL_ERROR "standard-library logger filtering or output failed: ${interpreter_log}")
endif()

set(showcase_source "${SOURCE_DIR}/test_program/stdlib_showcase.zp")
set(showcase_expected "Z+ Developer Report for ALICE\nConfiguration loaded\nMath check passed\nCollection check passed\nRandom check passed\nTime check passed\nReport written\n")
file(MAKE_DIRECTORY "${WORK_DIR}/showcase-interpreter")
execute_process(
    COMMAND "${ZP}" run "${showcase_source}" -- "${WORK_DIR}/showcase-interpreter" "Alice"
    RESULT_VARIABLE showcase_interpreter_result
    OUTPUT_VARIABLE showcase_interpreter_output
    ERROR_VARIABLE showcase_interpreter_error
    TIMEOUT 20
)
if(NOT showcase_interpreter_result EQUAL 0 OR
   NOT "${showcase_interpreter_output}" STREQUAL "${showcase_expected}")
    message(FATAL_ERROR "stdlib showcase interpreter run failed (${showcase_interpreter_result})\n${showcase_interpreter_output}${showcase_interpreter_error}")
endif()
file(READ "${WORK_DIR}/showcase-interpreter/report.txt" showcase_report)
if(NOT showcase_report MATCHES "Average: 25" OR
   NOT showcase_report MATCHES "Math check: passed")
    message(FATAL_ERROR "stdlib showcase report contents are incomplete: ${showcase_report}")
endif()

execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/stdlib/interpreter_math_constants.zp"
    RESULT_VARIABLE constants_result
    OUTPUT_VARIABLE constants_output
    ERROR_VARIABLE constants_error
    TIMEOUT 20
)
if(NOT constants_result EQUAL 0 OR NOT "${constants_output}" STREQUAL "constants-ok\n")
    message(FATAL_ERROR "interpreter math constants test failed (${constants_result})\n${constants_output}${constants_error}")
endif()

function(expect_interpreter_error name expected_message)
    execute_process(
        COMMAND "${ZP}" run "${SOURCE_DIR}/tests/stdlib/${name}.zp"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected_message}")
        message(FATAL_ERROR "${name}: expected interpreter error [${expected_message}]\n${output}${error}")
    endif()
endfunction()

expect_interpreter_error(invalid_math_domain "sqrt requires a non-negative number")
expect_interpreter_error(invalid_split_delimiter "split delimiter must not be empty")
expect_interpreter_error(invalid_args_index "command-line argument index is out of range")
expect_interpreter_error(invalid_average_empty "average cannot be computed for an empty array")
expect_interpreter_error(invalid_file_lines "writeLines entries must not contain line breaks")

set(fractional_math_source "${SOURCE_DIR}/tests/stdlib/native_fractional_math.zp")
set(fractional_math_expected "3.141592653589793\n1.4142135623730951\ninf\nnan\n-0\nnan comparison passed\n")
execute_process(
    COMMAND "${ZP}" check "${fractional_math_source}"
    RESULT_VARIABLE fractional_math_check_result
    OUTPUT_VARIABLE fractional_math_check_output
    ERROR_VARIABLE fractional_math_check_error
    TIMEOUT 20
)
if(NOT fractional_math_check_result EQUAL 0)
    message(FATAL_ERROR "fractional math check failed\n${fractional_math_check_output}${fractional_math_check_error}")
endif()
execute_process(
    COMMAND "${ZP}" run "${fractional_math_source}"
    RESULT_VARIABLE fractional_math_run_result
    OUTPUT_VARIABLE fractional_math_run_output
    ERROR_VARIABLE fractional_math_run_error
    TIMEOUT 20
)
if(NOT fractional_math_run_result EQUAL 0 OR
   NOT "${fractional_math_run_output}" STREQUAL "${fractional_math_expected}")
    message(FATAL_ERROR "fractional math interpreter mismatch\n${fractional_math_run_output}${fractional_math_run_error}")
endif()

if(HOST_WINDOWS)
    file(MAKE_DIRECTORY "${WORK_DIR}/native")
    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${WORK_DIR}/native/stdlib-smoke.exe"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "standard-library native build failed (${build_result})\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${WORK_DIR}/native/stdlib-smoke.exe" "${WORK_DIR}/native" "test-flag"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 20
    )
    if(NOT native_result EQUAL 0 OR NOT "${native_output}" STREQUAL "${expected}")
        message(FATAL_ERROR "standard-library native test failed (${native_result})\n${native_output}${native_error}")
    endif()
    file(READ "${WORK_DIR}/native/run.log" native_log)
    if(NOT native_log MATCHES "warning: visible" OR native_log MATCHES "filtered")
        message(FATAL_ERROR "native logger filtering or output failed: ${native_log}")
    endif()

    file(MAKE_DIRECTORY "${WORK_DIR}/showcase-native")
    execute_process(
        COMMAND "${ZP}" build "${showcase_source}"
                -o "${WORK_DIR}/showcase-native/stdlib-showcase.exe"
        RESULT_VARIABLE showcase_build_result
        OUTPUT_VARIABLE showcase_build_output
        ERROR_VARIABLE showcase_build_error
        TIMEOUT 20
    )
    if(NOT showcase_build_result EQUAL 0)
        message(FATAL_ERROR "stdlib showcase native build failed (${showcase_build_result})\n${showcase_build_output}${showcase_build_error}")
    endif()
    execute_process(
        COMMAND "${WORK_DIR}/showcase-native/stdlib-showcase.exe"
                "${WORK_DIR}/showcase-native" "Alice"
        RESULT_VARIABLE showcase_native_result
        OUTPUT_VARIABLE showcase_native_output
        ERROR_VARIABLE showcase_native_error
        TIMEOUT 20
    )
    if(NOT showcase_native_result EQUAL 0 OR
       NOT "${showcase_native_output}" STREQUAL "${showcase_expected}")
        message(FATAL_ERROR "stdlib showcase native run failed (${showcase_native_result})\n${showcase_native_output}${showcase_native_error}")
    endif()
    file(READ "${WORK_DIR}/showcase-native/report.txt" showcase_native_report)
    if(NOT showcase_native_report MATCHES "Average: 25" OR
       NOT showcase_native_report MATCHES "Math check: passed")
        message(FATAL_ERROR "native stdlib showcase report contents are incomplete: ${showcase_native_report}")
    endif()

    function(expect_native_error name expected_message)
        set(native_executable "${WORK_DIR}/native/${name}.exe")
        execute_process(
            COMMAND "${ZP}" build "${SOURCE_DIR}/tests/stdlib/${name}.zp"
                    -o "${native_executable}"
            RESULT_VARIABLE build_result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_error
            TIMEOUT 20
        )
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR "${name}: native build failed\n${build_output}${build_error}")
        endif()
        execute_process(
            COMMAND "${native_executable}"
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error
            TIMEOUT 20
        )
        if(run_result EQUAL 0 OR
           NOT "${run_output}${run_error}" MATCHES "${expected_message}")
            message(FATAL_ERROR "${name}: expected native error [${expected_message}]\n${run_output}${run_error}")
        endif()
    endfunction()

    function(expect_native_output name expected_output)
        set(native_executable "${WORK_DIR}/native/${name}.exe")
        execute_process(
            COMMAND "${ZP}" build "${SOURCE_DIR}/tests/stdlib/${name}.zp"
                    -o "${native_executable}"
            RESULT_VARIABLE build_result
            OUTPUT_VARIABLE build_output
            ERROR_VARIABLE build_error
            TIMEOUT 20
        )
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR "${name}: native build failed\n${build_output}${build_error}")
        endif()
        execute_process(
            COMMAND "${native_executable}"
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_output
            ERROR_VARIABLE run_error
            TIMEOUT 20
        )
        if(NOT run_result EQUAL 0 OR NOT "${run_output}" STREQUAL "${expected_output}")
            message(FATAL_ERROR "${name}: unexpected native result (${run_result})\n${run_output}${run_error}")
        endif()
    endfunction()

    expect_native_error(invalid_math_domain "sqrt requires a non-negative number")
    expect_native_error(invalid_split_delimiter "split delimiter must not be empty")
    expect_native_error(invalid_args_index "command-line argument index is out of range")
    expect_native_error(invalid_average_empty "average cannot be computed for an empty array")
    expect_native_error(invalid_file_lines "writeLines entries must not contain line breaks")
    expect_native_output(
        native_fractional_math
        "${fractional_math_expected}"
    )
endif()
