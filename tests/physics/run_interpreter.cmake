if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/physics/interpreter_smoke.zp"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 30
)
if(NOT result EQUAL 0 OR
   NOT stdout MATCHES "ZPhysics interpreter integration passed")
    message(FATAL_ERROR "interpreter physics smoke failed\n${stdout}${stderr}")
endif()
message(STATUS "${stdout}${stderr}")
