if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/test_program/audio/assets/tone.wav"
          "${SOURCE_DIR}/test_program/audio/assets/tone.ogg"
          "${SOURCE_DIR}/test_program/audio/assets/tone.mp3"
     DESTINATION "${WORK_DIR}")

if(WIN32)
    set(native_executable "${WORK_DIR}/audio-smoke.exe")
else()
    set(native_executable "${WORK_DIR}/audio-smoke.out")
endif()
execute_process(
    COMMAND "${ZP}" build "${SOURCE_DIR}/tests/audio/native_smoke.zp"
            -o "${native_executable}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
    TIMEOUT 60
)
if(NOT build_result EQUAL 0 OR NOT EXISTS "${native_executable}")
    message(FATAL_ERROR "native audio build failed\n${build_stdout}${build_stderr}")
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
   NOT run_stdout MATCHES "Audio native integration (passed|skipped)")
    message(FATAL_ERROR "native audio executable failed\n${run_stdout}${run_stderr}")
endif()
message(STATUS "${run_stdout}${run_stderr}")
