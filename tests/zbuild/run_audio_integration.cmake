if(NOT DEFINED ZBUILD OR NOT DEFINED ZP OR
   NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/tests/zbuild/audio_runtime/"
    DESTINATION "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/test_program/audio/assets/"
    DESTINATION "${WORK_DIR}/assets")

file(TO_CMAKE_PATH "${ZP}" compiler_path)
file(READ "${WORK_DIR}/zproject.toml" project_config)
string(REPLACE "[compiler]" "[compiler]\npath = \"${compiler_path}\""
    project_config "${project_config}")
file(WRITE "${WORK_DIR}/zproject.toml" "${project_config}")

function(run_zbuild expected_result)
    execute_process(
        COMMAND "${ZBUILD}" ${ARGN}
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 90
    )
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR
            "zbuild ${ARGN} returned ${result}, expected ${expected_result}\n${stdout}${stderr}")
    endif()
    set(last_output "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

run_zbuild(0 build)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "ZBuild did not build the audio target\n${last_output}")
endif()

if(WIN32)
    set(runtime "${WORK_DIR}/build/release/bin/zaudio_runtime.dll")
    set(executable "${WORK_DIR}/build/release/bin/app.exe")
else()
    set(runtime "${WORK_DIR}/build/release/bin/libzaudio_runtime.so")
    set(executable "${WORK_DIR}/build/release/bin/app.out")
endif()
if(NOT EXISTS "${runtime}" OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "ZBuild output is missing the native audio runtime or executable")
endif()
get_filename_component(runtime_name "${runtime}" NAME)

run_zbuild(0 run)
if(NOT last_output MATCHES "ZBuild audio integration (passed|skipped)")
    message(FATAL_ERROR "ZBuild audio executable did not run successfully\n${last_output}")
endif()

run_zbuild(0 package)
if(NOT EXISTS "${WORK_DIR}/build/release/package/${runtime_name}")
    message(FATAL_ERROR "ZBuild package is missing the audio runtime")
endif()
if(NOT EXISTS "${WORK_DIR}/build/release/package/assets/tone.ogg")
    message(FATAL_ERROR "ZBuild package is missing the audio fixture")
endif()

message(STATUS "ZBuild audio integration passed\n${last_output}")
