if(NOT DEFINED ZBUILD OR NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/test_program/zbuild_multi" DESTINATION "${WORK_DIR}")
set(project_dir "${WORK_DIR}/zbuild_multi")
file(READ "${project_dir}/src/main.zp" original_main_source)
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

execute_process(
    COMMAND "${ZP}" dependencies "${project_dir}/src/main.zp"
    RESULT_VARIABLE dependencies_result
    OUTPUT_VARIABLE dependencies_output
    ERROR_VARIABLE dependencies_error
)
if(NOT dependencies_result EQUAL 0 OR
   NOT dependencies_output MATCHES "message\\.zh" OR
   NOT dependencies_output MATCHES "math\\.zh")
    message(FATAL_ERROR
        "compiler dependency query did not report transitive imports\n"
        "${dependencies_output}${dependencies_error}")
endif()

run_zbuild(0 cache info)
if(NOT last_output MATCHES "entries=4" OR NOT last_output MATCHES "valid=4")
    message(FATAL_ERROR "cache info did not report the created records\n${last_output}")
endif()
run_zbuild(0 cache verify)
if(NOT last_output MATCHES "verification passed")
    message(FATAL_ERROR "cache verification rejected valid records\n${last_output}")
endif()

set(app_cache_record
    "${project_dir}/build/debug/metadata/cache/app-debug-native.sha256")
if(WIN32)
    set(app_output "${project_dir}/build/debug/artifacts/app.exe")
else()
    set(app_output "${project_dir}/build/debug/artifacts/app.out")
endif()
file(SHA256 "${app_cache_record}" cache_record_before_plan)
file(SHA256 "${app_output}" app_output_before_plan)
run_zbuild(0 plan --jobs 2)
if(NOT last_output MATCHES "\\[UP-TO-DATE\\] app" OR
   NOT last_output MATCHES "0 targets require rebuilding")
    message(FATAL_ERROR "plan did not identify up-to-date targets\n${last_output}")
endif()
run_zbuild(0 build --dry-run --no-cache)
if(NOT last_output MATCHES "\\[REBUILD\\]    app")
    message(FATAL_ERROR "no-cache dry-run did not plan a rebuild\n${last_output}")
endif()
file(SHA256 "${app_cache_record}" cache_record_after_plan)
file(SHA256 "${app_output}" app_output_after_plan)
if(NOT cache_record_before_plan STREQUAL cache_record_after_plan OR
   NOT app_output_before_plan STREQUAL app_output_after_plan)
    message(FATAL_ERROR "dry-run modified a cache record or build output")
endif()

file(REMOVE "${app_output}")
run_zbuild(0 plan --target app)
if(NOT last_output MATCHES "output is missing" OR EXISTS "${app_output}")
    message(FATAL_ERROR "missing output was not planned without modifying the project\n${last_output}")
endif()
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "a missing output was not rebuilt\n${last_output}")
endif()

run_zbuild(0 build --jobs 2)
if(NOT last_output MATCHES "up to date \\(content cache\\)")
    message(FATAL_ERROR "second build did not reuse cached outputs\n${last_output}")
endif()
run_zbuild(0 build --parallel)
if(NOT last_output MATCHES "cache_hits=4")
    message(FATAL_ERROR "--parallel did not execute with automatic workers\n${last_output}")
endif()

file(SHA256 "${app_cache_record}" cache_record_before_no_cache)
file(SHA256 "${project_dir}/build/debug/metadata/cache/stats.txt" stats_before_no_cache)
run_zbuild(0 build --target app --no-cache --jobs 1)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "--no-cache did not compile the selected target\n${last_output}")
endif()
file(SHA256 "${app_cache_record}" cache_record_after_no_cache)
file(SHA256 "${project_dir}/build/debug/metadata/cache/stats.txt" stats_after_no_cache)
if(NOT cache_record_before_no_cache STREQUAL cache_record_after_no_cache OR
   NOT stats_before_no_cache STREQUAL stats_after_no_cache)
    message(FATAL_ERROR "--no-cache read/write bypass changed persistent cache data")
endif()

file(WRITE
    "${project_dir}/build/debug/metadata/cache/app-debug-native.sha256"
    "corrupt cache record\n")
run_zbuild(1 cache verify)
if(NOT last_output MATCHES "invalid=1" OR
   NOT last_output MATCHES "verification found invalid entries")
    message(FATAL_ERROR "cache verify did not identify corrupted metadata\n${last_output}")
endif()
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
run_zbuild(0 plan --target app)
if(NOT last_output MATCHES "\\[REBUILD\\]    app" OR
   NOT last_output MATCHES "source or build configuration changed")
    message(FATAL_ERROR "plan missed an imported-module content change\n${last_output}")
endif()
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "module content change did not invalidate the cache\n${last_output}")
endif()
run_zbuild(0 build --target app_tests)
if(NOT last_output MATCHES "\\[build\\] app_tests complete")
    message(FATAL_ERROR "changed dependency output did not invalidate its dependent target\n${last_output}")
endif()

string(REPLACE "message.text()" "missing_function()" failed_main_source
    "${original_main_source}")
if(failed_main_source STREQUAL original_main_source)
    message(FATAL_ERROR "failed-build fixture could not modify the source")
endif()
file(WRITE "${project_dir}/src/main.zp" "${failed_main_source}")
file(SHA256 "${project_dir}/build/debug/metadata/cache/stats.txt" stats_before_failed_build)
run_zbuild(1 build --target app)
if(EXISTS "${app_output}")
    message(FATAL_ERROR "failed compilation left a runnable stale executable")
endif()
if(EXISTS "${app_cache_record}")
    message(FATAL_ERROR "failed compilation left a successful cache record")
endif()
file(SHA256 "${project_dir}/build/debug/metadata/cache/stats.txt" stats_after_failed_build)
if(stats_before_failed_build STREQUAL stats_after_failed_build)
    message(FATAL_ERROR "failed build operations were not included in cache statistics")
endif()
file(WRITE "${project_dir}/src/main.zp" "${original_main_source}")
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "valid source did not recover after failed compilation\n${last_output}")
endif()

file(RENAME "${project_dir}/src/message.zh" "${project_dir}/src/message.renamed")
run_zbuild(1 run --target app)
if(last_output MATCHES "ZBuild integration works")
    message(FATAL_ERROR "run used a stale executable after an imported module was removed")
endif()
if(WIN32)
    set(app_output "${project_dir}/build/debug/artifacts/app.exe")
else()
    set(app_output "${project_dir}/build/debug/artifacts/app.out")
endif()
if(EXISTS "${app_output}")
    message(FATAL_ERROR "failed build left a stale executable available for use")
endif()
file(RENAME "${project_dir}/src/message.renamed" "${project_dir}/src/message.zh")
run_zbuild(0 build --target app)

run_zbuild(0 build --release --target app)
if(WIN32)
    set(release_output "${project_dir}/build/release/artifacts/app.exe")
else()
    set(release_output "${project_dir}/build/release/artifacts/app.out")
endif()
if(NOT EXISTS "${release_output}" OR NOT EXISTS "${app_output}")
    message(FATAL_ERROR "Debug and Release artifacts were not kept separate")
endif()

run_zbuild(0 check --target app)
if(NOT last_output MATCHES "\\[check\\] app passed")
    message(FATAL_ERROR "configured target check did not run\n${last_output}")
endif()

run_zbuild(0 clean --debug)
if(EXISTS "${app_output}" OR NOT EXISTS "${project_dir}/src/main.zp")
    message(FATAL_ERROR "clean removed source files or left a Debug artifact behind")
endif()
run_zbuild(0 rebuild --target app)
if(NOT EXISTS "${app_output}" OR NOT EXISTS "${project_dir}/src/main.zp")
    message(FATAL_ERROR "rebuild did not recreate output while preserving sources")
endif()
run_zbuild(0 build app)
if(NOT last_output MATCHES "up to date \\(content cache\\)")
    message(FATAL_ERROR "positional target selection did not reuse the target cache\n${last_output}")
endif()

run_zbuild(0 cache clean)
if(EXISTS "${project_dir}/build/debug/metadata/cache")
    message(FATAL_ERROR "cache clean left ZPBuild cache data behind")
endif()
if(NOT EXISTS "${app_output}")
    message(FATAL_ERROR "cache clean removed the target artifact")
endif()
run_zbuild(0 cache info)
if(NOT last_output MATCHES "entries=0")
    message(FATAL_ERROR "cache info reported entries after cache clean\n${last_output}")
endif()
run_zbuild(0 build --target app)
if(NOT last_output MATCHES "\\[build\\] app complete")
    message(FATAL_ERROR "cache clean did not cause an actual rebuild\n${last_output}")
endif()
run_zbuild(0 cache verify)
if(NOT last_output MATCHES "verification passed")
    message(FATAL_ERROR "new cache entry did not verify\n${last_output}")
endif()

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
    COMMAND "${ZBUILD}" new "${WORK_DIR}/generated-executable"
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

file(MAKE_DIRECTORY "${WORK_DIR}/custom-project/src")
file(WRITE "${WORK_DIR}/custom-project/src/main.zp" "start:\nstop\n")
file(TO_CMAKE_PATH "${ZP}" compiler_path)
file(TO_CMAKE_PATH "${CMAKE_COMMAND}" cmake_executable)
file(WRITE "${WORK_DIR}/custom-project/zproject.toml"
    "[project]\nname = \"CustomProject\"\nentry = \"src/main.zp\"\n"
    "\n[build]\nbackend = \"native\"\noutput = \"build\"\n"
    "\n[sources]\ndirectories = [\"src\"]\nexclude = [\"build\"]\n"
    "\n[compiler]\npath = \"${compiler_path}\"\n"
    "\n[targets]\ndefault = \"application\"\n"
    "\n[target.application]\ntype = \"executable\"\nentry = \"src/main.zp\"\n"
    "dependencies = [\"prepare\"]\n"
    "\n[target.prepare]\ntype = \"custom\"\n"
    "command = [\"${cmake_executable}\", \"-E\", \"touch\", "
    "\"{output_dir}/prepared.txt\"]\n")
execute_process(
    COMMAND "${ZBUILD}" build application
    WORKING_DIRECTORY "${WORK_DIR}/custom-project"
    RESULT_VARIABLE custom_result
    OUTPUT_VARIABLE custom_stdout
    ERROR_VARIABLE custom_stderr
    TIMEOUT 60
)
if(NOT custom_result EQUAL 0 OR
   NOT EXISTS "${WORK_DIR}/custom-project/build/debug/artifacts/prepared.txt")
    message(FATAL_ERROR
        "custom dependency target did not complete before the application build\n"
        "${custom_stdout}${custom_stderr}")
endif()

foreach(process_output IN ITEMS
    "${project_dir}/build/debug/metadata/cache/app.log"
    "${project_dir}/build/debug/metadata/cache/other.log"
    "${project_dir}/build/debug/metadata/cache/interpreter_app.log"
    "${project_dir}/build/debug/metadata/cache/app_tests.log"
    "${project_dir}/build/debug/run.log"
    "${project_dir}/build/debug/cache/app_tests.test.log")
    if(EXISTS "${process_output}")
        message(FATAL_ERROR "ZBuild left an unnecessary process-output file: ${process_output}")
    endif()
endforeach()

file(GLOB_RECURSE temporary_artifacts
    "${project_dir}/build/*.zbuild-tmp-*"
    "${project_dir}/build/*.sha256.tmp")
if(temporary_artifacts)
    message(FATAL_ERROR "ZBuild left temporary build artifacts: ${temporary_artifacts}")
endif()

message(STATUS "ZBuild integration tests passed")
