if(NOT DEFINED ZBUILD OR NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/test_program/zbuild_multi" DESTINATION "${WORK_DIR}")
set(project_dir "${WORK_DIR}/zbuild_multi")
file(REMOVE_RECURSE "${project_dir}/build")
file(TO_CMAKE_PATH "${ZP}" compiler_path)
file(READ "${project_dir}/zproject.toml" project_config)
string(REPLACE "[compiler]" "[compiler]\npath = \"${compiler_path}\""
    project_config "${project_config}")
file(WRITE "${project_dir}/zproject.toml" "${project_config}")

function(run_zbuild expected_result)
    execute_process(
        COMMAND "${ZBUILD}" ${ARGN}
        WORKING_DIRECTORY "${project_dir}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 60
    )
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR
            "zbuild ${ARGN} returned ${result}, expected ${expected_result}\n${stdout}${stderr}")
    endif()
    set(last_output "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

run_zbuild(0 configure)
run_zbuild(0 build --jobs 2)
if(NOT last_output MATCHES "\\[build\\] app complete" OR
   NOT last_output MATCHES "\\[build\\] other complete" OR
   NOT last_output MATCHES "\\[build\\] interpreter_app complete" OR
   NOT last_output MATCHES "\\[build\\] app_tests complete")
    message(FATAL_ERROR "build did not compile both configured targets\n${last_output}")
endif()

run_zbuild(0 build --jobs 2)
if(NOT last_output MATCHES "up to date \\(content cache\\)")
    message(FATAL_ERROR "second build did not reuse cached outputs\n${last_output}")
endif()

file(WRITE "${project_dir}/build/cache/app-debug-native.sha256" "corrupt cache record\n")
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "\\[cache\\] invalid record" OR
   NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "corrupt cache metadata was not detected and rebuilt\n${last_output}")
endif()

file(APPEND "${project_dir}/src/unused.zh" "\n")
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "up to date \\(content cache\\)" OR
   last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "an unrelated source change unnecessarily rebuilt app\n${last_output}")
endif()

run_zbuild(0 run --target app)
if(NOT last_output MATCHES "ZBuild integration works")
    message(FATAL_ERROR "generated executable did not run as expected\n${last_output}")
endif()

run_zbuild(0 run --target interpreter_app)
if(NOT last_output MATCHES "Interpreter target works")
    message(FATAL_ERROR "interpreter target did not run as expected\n${last_output}")
endif()

run_zbuild(0 test --target app_tests --verbose)
if(NOT last_output MATCHES "ZBuild tests passed" OR
   NOT last_output MATCHES "1 passed, 0 failed")
    message(FATAL_ERROR "configured test target did not execute\n${last_output}")
endif()

run_zbuild(0 package --target app)
if(WIN32)
    set(executable "${project_dir}/build/debug/package/app.exe")
else()
    set(executable "${project_dir}/build/debug/package/app.out")
endif()
if(NOT EXISTS "${executable}" OR
   NOT EXISTS "${project_dir}/build/debug/assets/fixture.txt" OR
   NOT EXISTS "${project_dir}/build/debug/package/assets/fixture.txt")
    message(FATAL_ERROR "package output is missing the executable or project assets")
endif()
if(WIN32 AND NOT EXISTS "${project_dir}/build/debug/package/zgraphics_runtime.dll")
    message(FATAL_ERROR "Windows package output is missing the graphics runtime DLL")
endif()

file(APPEND "${project_dir}/src/message.zh" "\n")
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "module content change did not invalidate the cache\n${last_output}")
endif()

file(RENAME "${project_dir}/src/message.zh" "${project_dir}/src/message.renamed")
run_zbuild(1 run --target app)
if(last_output MATCHES "ZBuild integration works")
    message(FATAL_ERROR "run used a stale executable after an imported module was removed")
endif()
if(WIN32)
    set(app_output "${project_dir}/build/debug/bin/app.exe")
else()
    set(app_output "${project_dir}/build/debug/bin/app.out")
endif()
if(EXISTS "${app_output}")
    message(FATAL_ERROR "failed build left a stale executable available for use")
endif()
file(RENAME "${project_dir}/src/message.renamed" "${project_dir}/src/message.zh")
run_zbuild(0 build --target app)

file(READ "${project_dir}/zproject.toml" config)
string(REPLACE "dependencies = [\"app\"]" "dependencies = [\"app_tests\"]" config "${config}")
file(WRITE "${project_dir}/zproject.toml" "${config}")
run_zbuild(1 build --target app_tests)
if(NOT last_output MATCHES "circular target dependency")
    message(FATAL_ERROR "target cycle was not diagnosed\n${last_output}")
endif()

execute_process(
    COMMAND "${ZBUILD}" init "${WORK_DIR}/generated-library" --template library
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE init_result
    OUTPUT_VARIABLE init_stdout
    ERROR_VARIABLE init_stderr
)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "library template initialization failed\n${init_stdout}${init_stderr}")
endif()
execute_process(
    COMMAND "${ZBUILD}" test --verbose
    WORKING_DIRECTORY "${WORK_DIR}/generated-library"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
    TIMEOUT 60
)
if(NOT run_result EQUAL 0 OR NOT run_stdout MATCHES "Library greeting test passed")
    message(FATAL_ERROR "library template failed to build and test\n${run_stdout}${run_stderr}")
endif()

execute_process(
    COMMAND "${ZBUILD}" init "${WORK_DIR}/generated-executable"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE init_result
    OUTPUT_VARIABLE init_stdout
    ERROR_VARIABLE init_stderr
)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "executable template initialization failed\n${init_stdout}${init_stderr}")
endif()
execute_process(
    COMMAND "${ZBUILD}" run
    WORKING_DIRECTORY "${WORK_DIR}/generated-executable"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
    TIMEOUT 60
)
if(NOT run_result EQUAL 0 OR NOT run_stdout MATCHES "Hello, Z\\+ developer!")
    message(FATAL_ERROR "executable template failed to build and run\n${run_stdout}${run_stderr}")
endif()

foreach(process_output IN ITEMS
    "${project_dir}/build/cache/app.log"
    "${project_dir}/build/cache/other.log"
    "${project_dir}/build/cache/interpreter_app.log"
    "${project_dir}/build/cache/app_tests.log"
    "${project_dir}/build/debug/run.log"
    "${project_dir}/build/debug/cache/app_tests.test.log")
    if(EXISTS "${process_output}")
        message(FATAL_ERROR "ZBuild left an unnecessary process-output file: ${process_output}")
    endif()
endforeach()

file(GLOB_RECURSE temporary_artifacts
    "${project_dir}/build/*.zbuild-tmp"
    "${project_dir}/build/*.sha256.tmp")
if(temporary_artifacts)
    message(FATAL_ERROR "ZBuild left temporary build artifacts: ${temporary_artifacts}")
endif()

message(STATUS "ZBuild integration tests passed")
