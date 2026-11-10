if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/test_program/audio/assets/tone.wav"
          "${SOURCE_DIR}/test_program/audio/assets/tone.ogg"
          "${SOURCE_DIR}/test_program/audio/assets/tone.mp3"
     DESTINATION "${WORK_DIR}")

execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/audio/interpreter_smoke.zp"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 30
)
if(NOT result EQUAL 0 OR
   NOT stdout MATCHES "Audio interpreter integration (passed|skipped)")
    message(FATAL_ERROR "interpreter audio smoke failed\n${stdout}${stderr}")
endif()
message(STATUS "${stdout}${stderr}")
