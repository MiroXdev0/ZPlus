if(NOT DEFINED ZBUILD OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/tests/zbuild/backend_runtime/"
    DESTINATION "${WORK_DIR}")

execute_process(
    COMMAND "${ZBUILD}" run
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 40
)
if(NOT result EQUAL 0 OR NOT "${output}" MATCHES "zbuild-backend-runtime-passed")
    message(FATAL_ERROR "ZBuild backend integration failed (${result})\n${output}${error}")
endif()

if(HOST_WINDOWS AND
   NOT EXISTS "${WORK_DIR}/build/release/bin/app.exe")
    message(FATAL_ERROR "ZBuild did not publish the project native executable")
endif()
