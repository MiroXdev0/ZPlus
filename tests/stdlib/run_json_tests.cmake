if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(source "${SOURCE_DIR}/tests/stdlib/json_smoke.zp")
set(expected
    "json-valid\nMira 🚀\n{\"ok\":true}\ntrue\n2\n3\nobject\n{\"n\":1}\n\"a\\\"b\"\ninvalid-json-rejected\n")

execute_process(
    COMMAND "${ZP}" run "${source}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
    TIMEOUT 15
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "JSON interpreter integration failed (${run_result})\n${run_output}${run_error}")
endif()
if(NOT "${run_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "Unexpected JSON interpreter output\nExpected: [${expected}]\nActual: [${run_output}]")
endif()

if(HOST_WINDOWS)
    set(native_source "${SOURCE_DIR}/tests/stdlib/json_native.zp")
    set(executable "${WORK_DIR}/json_native.exe")
    execute_process(
        COMMAND "${ZP}" build "${native_source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "JSON native build failed (${build_result})\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 15
    )
    set(native_expected "number-ok\nboolean-ok\n\"second\"\narray\n{\"ok\":true}\n")
    if(NOT native_result EQUAL 0 OR NOT "${native_output}" STREQUAL "${native_expected}")
        message(FATAL_ERROR "JSON native integration failed (${native_result})\nExpected: [${native_expected}]\nActual: [${native_output}]\n${native_error}")
    endif()
endif()
