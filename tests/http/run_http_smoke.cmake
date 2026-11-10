if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(source "${SOURCE_DIR}/tests/http/http_runtime_smoke.zp")
set(expected
    "http-started\nhttp-port-assigned\nhttp-running\nhttp-poll-completed\nhttp-stopped\n")

execute_process(
    COMMAND "${ZP}" run "${source}"
    RESULT_VARIABLE interpreter_result
    OUTPUT_VARIABLE interpreter_output
    ERROR_VARIABLE interpreter_error
    TIMEOUT 20
)
if(NOT interpreter_result EQUAL 0 OR NOT "${interpreter_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "HTTP interpreter integration failed (${interpreter_result})\nExpected: [${expected}]\nActual: [${interpreter_output}]\n${interpreter_error}")
endif()

if(HOST_WINDOWS)
    set(executable "${WORK_DIR}/http_runtime_smoke.exe")
    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "HTTP native build failed (${build_result})\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 20
    )
    if(NOT native_result EQUAL 0 OR NOT "${native_output}" STREQUAL "${expected}")
        message(FATAL_ERROR "HTTP native integration failed (${native_result})\nExpected: [${expected}]\nActual: [${native_output}]\n${native_error}")
    endif()
endif()
