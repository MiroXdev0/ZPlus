if(NOT DEFINED ZBUILD OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/tests/zbuild/backend_runtime/"
    DESTINATION "${WORK_DIR}")

execute_process(
    COMMAND "${ZBUILD}" build
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 40
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ZBuild backend build failed (${result})\n${output}${error}")
endif()

if(HOST_WINDOWS)
    set(executable "${WORK_DIR}/build/release/bin/app.exe")
    set(input_runtime "${WORK_DIR}/build/release/bin/zplus_input_runtime.dll")
    set(package_input_runtime
        "${WORK_DIR}/build/release/package/zplus_input_runtime.dll")
else()
    set(executable "${WORK_DIR}/build/release/bin/app.out")
    set(input_runtime "${WORK_DIR}/build/release/bin/libzplus_input_runtime.so")
    set(package_input_runtime
        "${WORK_DIR}/build/release/package/libzplus_input_runtime.so")
endif()
if(NOT EXISTS "${executable}")
    message(FATAL_ERROR "ZBuild did not publish the project native executable")
endif()
if(NOT EXISTS "${input_runtime}")
    message(FATAL_ERROR "ZBuild did not stage the terminal input runtime")
endif()

set(input_file "${WORK_DIR}/terminal-input.stdin")
file(WRITE "${input_file}" "zbuild-terminal-input\n")
execute_process(
    COMMAND "${executable}"
    INPUT_FILE "${input_file}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 15
)
if(NOT result EQUAL 0 OR
   NOT "${output}" STREQUAL
       "zbuild-backend-runtime-passed\nzbuild-terminal-input-passed\n")
    message(FATAL_ERROR
        "ZBuild native terminal input failed (${result})\n${output}${error}")
endif()

execute_process(
    COMMAND "${ZBUILD}" package
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 40
)
if(NOT result EQUAL 0 OR
   NOT EXISTS "${package_input_runtime}")
    message(FATAL_ERROR "ZBuild terminal input package failed (${result})\n${output}${error}")
endif()
