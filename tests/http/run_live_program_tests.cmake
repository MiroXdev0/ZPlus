if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
file(MAKE_DIRECTORY "${WORK_DIR}")
foreach(name IN ITEMS hello_server rest_api multi_client_server external_api_client)
    execute_process(
        COMMAND "${ZP}" build
            "${SOURCE_DIR}/test_program/backend/${name}.zp"
            -o "${WORK_DIR}/${name}.exe"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "Native backend example ${name} failed to build\n${build_output}${build_error}")
    endif()
endforeach()

execute_process(
    COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File "${SOURCE_DIR}/tests/http/run_live_program_tests.ps1"
        -Zp "${ZP}"
        -SourceRoot "${SOURCE_DIR}"
        -WorkDir "${WORK_DIR}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
    TIMEOUT 120
)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Live Z+ HTTP programs failed (${test_result})\n${test_output}${test_error}")
endif()
