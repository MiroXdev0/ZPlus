if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(program "${SOURCE_DIR}/tests/runtime/error_handling.zp")
set(expected "try cleanup\ninner: boom\nraised again\nnested\nlast\nreturned\n7\nruntime: cannot pop from an empty array\ncleanup: cleanup failure\nrethrow: boom\nouter\n")

execute_process(
    COMMAND "${ZP}" check "${program}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
    TIMEOUT 20
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "error-handling program failed checking\n${check_output}${check_error}")
endif()

foreach(invalid_source IN ITEMS
    "${SOURCE_DIR}/tests/runtime/invalid_try.zp"
    "${SOURCE_DIR}/tests/runtime/invalid_defer.zp")
    execute_process(
        COMMAND "${ZP}" check "${invalid_source}"
        RESULT_VARIABLE invalid_result
        OUTPUT_VARIABLE invalid_output
        ERROR_VARIABLE invalid_error
        TIMEOUT 20
    )
    if(invalid_result EQUAL 0)
        message(FATAL_ERROR "invalid error-handling syntax unexpectedly passed: ${invalid_source}")
    endif()
endforeach()

execute_process(
    COMMAND "${ZP}" run "${program}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
    TIMEOUT 20
)
if(NOT run_result EQUAL 0 OR NOT "${run_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "error-handling output mismatch (${run_result})\nexpected:\n${expected}\nactual:\n${run_output}${run_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/runtime/unhandled_throw.zp"
    RESULT_VARIABLE unhandled_result
    OUTPUT_VARIABLE unhandled_output
    ERROR_VARIABLE unhandled_error
    TIMEOUT 20
)
if(unhandled_result EQUAL 0 OR
   NOT "${unhandled_output}${unhandled_error}" MATCHES "unhandled problem")
    message(FATAL_ERROR "unhandled throw did not produce an error\n${unhandled_output}${unhandled_error}")
endif()

set(native_program "${SOURCE_DIR}/tests/runtime/native_error_handling.zp")
set(native_expected
    "try cleanup\ninner: boom\nraised again\nnested\nlast\nreturn cleanup\n7\ncleanup failure\nrethrow: boom\nouter\n")
execute_process(
    COMMAND "${ZP}" run "${native_program}"
    RESULT_VARIABLE native_interpreter_result
    OUTPUT_VARIABLE native_interpreter_output
    ERROR_VARIABLE native_interpreter_error
    TIMEOUT 30
)
if(NOT native_interpreter_result EQUAL 0 OR
   NOT "${native_interpreter_output}" STREQUAL "${native_expected}")
    message(FATAL_ERROR
        "native error-handling fixture failed in interpreter mode\n${native_interpreter_output}${native_interpreter_error}")
endif()

set(native_executable "${WORK_DIR}/native_error_handling.exe")
if(WIN32)
execute_process(
    COMMAND "${ZP}" build "${native_program}" -o "${native_executable}"
    RESULT_VARIABLE native_build_result
    OUTPUT_VARIABLE native_build_output
    ERROR_VARIABLE native_build_error
    TIMEOUT 30
)
if(NOT native_build_result EQUAL 0)
    message(FATAL_ERROR
        "native exception/defer program failed to build\n${native_build_output}${native_build_error}")
endif()

execute_process(
    COMMAND "${native_executable}"
    RESULT_VARIABLE native_run_result
    OUTPUT_VARIABLE native_run_output
    ERROR_VARIABLE native_run_error
    TIMEOUT 30
)
if(NOT native_run_result EQUAL 0 OR
   NOT "${native_run_output}" STREQUAL "${native_expected}")
    message(FATAL_ERROR
        "native/interpreter exception/defer parity failed (${native_run_result})\nexpected:\n${native_expected}\nactual:\n${native_run_output}${native_run_error}")
endif()

set(full_native_executable "${WORK_DIR}/error_handling.native")
execute_process(
    COMMAND "${ZP}" build "${program}" -o "${full_native_executable}"
    RESULT_VARIABLE full_native_build_result
    OUTPUT_VARIABLE full_native_build_output
    ERROR_VARIABLE full_native_build_error
    TIMEOUT 30
)
if(NOT full_native_build_result EQUAL 0)
    message(FATAL_ERROR
        "full native error-handling fixture failed to build\n${full_native_build_output}${full_native_build_error}")
endif()
execute_process(
    COMMAND "${full_native_executable}"
    RESULT_VARIABLE full_native_run_result
    OUTPUT_VARIABLE full_native_run_output
    ERROR_VARIABLE full_native_run_error
    TIMEOUT 30
)
if(NOT full_native_run_result EQUAL 0 OR
   NOT "${full_native_run_output}" STREQUAL "${expected}")
    message(FATAL_ERROR
        "full native/interpreter error-handling parity failed (${full_native_run_result})\nexpected:\n${expected}\nactual:\n${full_native_run_output}${full_native_run_error}")
endif()

set(native_unhandled "${WORK_DIR}/unhandled_throw.native")
execute_process(
    COMMAND "${ZP}" build "${SOURCE_DIR}/tests/runtime/unhandled_throw.zp"
            -o "${native_unhandled}"
    RESULT_VARIABLE unhandled_build_result
    OUTPUT_VARIABLE unhandled_build_output
    ERROR_VARIABLE unhandled_build_error
    TIMEOUT 30
)
if(NOT unhandled_build_result EQUAL 0)
    message(FATAL_ERROR
        "uncaught native throw failed to build\n${unhandled_build_output}${unhandled_build_error}")
endif()
execute_process(
    COMMAND "${native_unhandled}"
    RESULT_VARIABLE native_unhandled_result
    OUTPUT_VARIABLE native_unhandled_output
    ERROR_VARIABLE native_unhandled_error
    TIMEOUT 20
)
if(native_unhandled_result EQUAL 0 OR
   NOT "${native_unhandled_output}${native_unhandled_error}" MATCHES "unhandled problem")
    message(FATAL_ERROR
        "uncaught native exception was not reported (${native_unhandled_result})\n${native_unhandled_output}${native_unhandled_error}")
endif()
endif()
