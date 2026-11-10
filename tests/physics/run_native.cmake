if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
if(WIN32)
    set(native_executable "${WORK_DIR}/physics-smoke.exe")
else()
    set(native_executable "${WORK_DIR}/physics-smoke.out")
endif()
execute_process(
    COMMAND "${ZP}" build "${SOURCE_DIR}/tests/physics/native_smoke.zp"
            -o "${native_executable}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
    TIMEOUT 60
)
if(NOT build_result EQUAL 0 OR NOT EXISTS "${native_executable}")
    message(FATAL_ERROR "native physics build failed\n${build_stdout}${build_stderr}")
endif()

execute_process(
    COMMAND "${native_executable}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
    TIMEOUT 30
)
if(NOT run_result EQUAL 0 OR
   NOT run_stdout MATCHES "ZPhysics native integration passed")
    message(FATAL_ERROR "native physics executable failed\n${run_stdout}${run_stderr}")
endif()
message(STATUS "${run_stdout}${run_stderr}")
