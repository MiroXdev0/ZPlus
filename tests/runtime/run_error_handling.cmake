if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(program "${SOURCE_DIR}/tests/runtime/error_handling.zp")
set(expected "try cleanup\ninner: boom\nraised again\nnested\nlast\nreturned\n7\nruntime: cannot pop from an empty array\nouter\n")

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

set(native_output "${WORK_DIR}/error_handling.native")
execute_process(
    COMMAND "${ZP}" build "${program}" -o "${native_output}"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_output_text
    ERROR_VARIABLE native_error
    TIMEOUT 30
)
if(native_result EQUAL 0)
    message(FATAL_ERROR "native backend unexpectedly accepted interpreter-only exception opcodes")
endif()
if(NOT "${native_output_text}${native_error}" MATCHES
   "not supported")
    message(FATAL_ERROR
        "native backend did not report the unsupported feature clearly\n${native_output_text}${native_error}")
endif()
