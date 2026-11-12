if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(source "${SOURCE_DIR}/tests/installer/platform_api.zp")

execute_process(
    COMMAND "${ZP}" run "${source}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE interpreted_result
    OUTPUT_VARIABLE interpreted_output
    ERROR_VARIABLE interpreted_error
    TIMEOUT 60
)
if(NOT interpreted_result EQUAL 0 OR
   NOT "${interpreted_output}" MATCHES "installer-platform-api-passed" OR
   "${interpreted_output}" MATCHES "failed")
    message(FATAL_ERROR
        "Installer platform interpreter integration failed (${interpreted_result})\n"
        "${interpreted_output}${interpreted_error}")
endif()

if(WIN32)
    set(native "${WORK_DIR}/installer-platform-api.exe")
else()
    set(native "${WORK_DIR}/installer-platform-api")
endif()
execute_process(
    COMMAND "${ZP}" build "${source}" -o "${native}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
    TIMEOUT 60
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "Installer platform native build failed (${build_result})\n"
        "${build_output}${build_error}")
endif()
execute_process(
    COMMAND "${native}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_output
    ERROR_VARIABLE native_error
    TIMEOUT 60
)
if(NOT native_result EQUAL 0 OR
   NOT "${native_output}" MATCHES "installer-platform-api-passed" OR
   "${native_output}" MATCHES "failed")
    message(FATAL_ERROR
        "Installer platform native integration failed (${native_result})\n"
        "${native_output}${native_error}")
endif()
