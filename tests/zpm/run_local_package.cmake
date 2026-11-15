if(NOT DEFINED ZPM OR NOT DEFINED ZBUILD OR NOT DEFINED ZP OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZPM, ZBUILD, ZP, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(package_dir "${WORK_DIR}/source package")
set(core_dir "${WORK_DIR}/greeting core")
set(core_cache_project "${WORK_DIR}/core cache project")
set(consumer_dir "${WORK_DIR}/consumer project")
set(offline_dir "${WORK_DIR}/offline consumer")
set(cache_dir "${WORK_DIR}/cache")
file(MAKE_DIRECTORY
    "${package_dir}" "${core_dir}" "${core_cache_project}"
    "${consumer_dir}" "${offline_dir}")

function(run_zpm expected_result working_directory)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "ZPM_CACHE_DIR=${cache_dir}"
            "${ZPM}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 60
    )
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR
            "zpm ${ARGN} returned ${result}, expected ${expected_result}\n${stdout}${stderr}")
    endif()
    set(last_output "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

function(run_zpbuild expected_result working_directory)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "ZPM_CACHE_DIR=${cache_dir}"
            "${ZBUILD}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 90
    )
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR
            "zpbuild ${ARGN} returned ${result}, expected ${expected_result}\n${stdout}${stderr}")
    endif()
    set(last_output "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

execute_process(
    COMMAND "${ZPM}" init "greeting-core"
    WORKING_DIRECTORY "${core_dir}"
    RESULT_VARIABLE init_result
    OUTPUT_VARIABLE init_output
    ERROR_VARIABLE init_error
    TIMEOUT 30
)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "core zpm init failed\n${init_output}${init_error}")
endif()
file(WRITE "${core_dir}/src/main.zh"
"start {\n"
"    pub fn greeting(): string {\n"
"        return \"Greetings via transitive Z+ package\"\n"
"    } end fn\n"
"} stop\n")
set(core_archive "${WORK_DIR}/greeting-core-0.1.0.zpm")
run_zpm(0 "${core_dir}" pack "${core_dir}" "${core_archive}")
if(NOT EXISTS "${core_archive}")
    message(FATAL_ERROR "zpm pack did not create its transitive package archive")
endif()

execute_process(
    COMMAND "${ZPM}" init "cache-project"
    WORKING_DIRECTORY "${core_cache_project}"
    RESULT_VARIABLE cache_init_result
    OUTPUT_VARIABLE cache_init_output
    ERROR_VARIABLE cache_init_error
    TIMEOUT 30
)
if(NOT cache_init_result EQUAL 0)
    message(FATAL_ERROR "cache project zpm init failed\n${cache_init_output}${cache_init_error}")
endif()
run_zpm(0 "${core_cache_project}" install "${core_archive}")
run_zpm(0 "${package_dir}" init "hello-package")
file(WRITE "${package_dir}/src/main.zh"
"use \"greeting-core\" as core\n"
"start {\n"
"    pub fn greeting(): string {\n"
"        return core.greeting()\n"
"    } end fn\n"
"} stop\n")
file(READ "${package_dir}/zpm.json" package_manifest)
string(REPLACE "\"dependencies\":{}"
    "\"dependencies\":{\"greeting-core\":\"^0.1.0\"}"
    package_manifest "${package_manifest}")
file(WRITE "${package_dir}/zpm.json" "${package_manifest}")
run_zpbuild(0 "${package_dir}" build)
run_zpm(0 "${package_dir}" test)

set(archive "${WORK_DIR}/hello-package-0.1.0.zpm")
run_zpm(0 "${package_dir}" pack "${package_dir}" "${archive}")
if(NOT EXISTS "${archive}")
    message(FATAL_ERROR "zpm pack did not create its package archive")
endif()

execute_process(
    COMMAND "${ZPM}" init "consumer"
    WORKING_DIRECTORY "${consumer_dir}"
    RESULT_VARIABLE consumer_init_result
    OUTPUT_VARIABLE consumer_init_output
    ERROR_VARIABLE consumer_init_error
    TIMEOUT 30
)
if(NOT consumer_init_result EQUAL 0)
    message(FATAL_ERROR
        "consumer zpm init failed\n${consumer_init_output}${consumer_init_error}")
endif()
file(WRITE "${consumer_dir}/src/main.zp"
"use \"hello-package\" as hello\n"
"start {\n"
"    say(hello.greeting())\n"
"} stop\n")
run_zpm(0 "${consumer_dir}" install "${archive}")
if(NOT EXISTS "${consumer_dir}/z+_packages/hello-package/src/main.zh" OR
   NOT EXISTS "${consumer_dir}/z+_packages/greeting-core/src/main.zh" OR
   NOT EXISTS "${consumer_dir}/zpm-lock.json")
    message(FATAL_ERROR "transitive archive install omitted package source or lockfile")
endif()
run_zpm(0 "${consumer_dir}" install --locked)
if(NOT last_output MATCHES "Installed 2 package")
    message(FATAL_ERROR "locked install did not report the resolved package\n${last_output}")
endif()
run_zpm(0 "${consumer_dir}" list)
if(NOT last_output MATCHES "hello-package 0\\.1\\.0" OR
   NOT last_output MATCHES "greeting-core 0\\.1\\.0")
    message(FATAL_ERROR "transitive packages were not listed\n${last_output}")
endif()

run_zpbuild(0 "${consumer_dir}" build)
run_zpbuild(0 "${consumer_dir}" run)
if(NOT last_output MATCHES "Greetings via transitive Z\\+ package")
    message(FATAL_ERROR "ZPBuild did not run the imported package code\n${last_output}")
endif()
execute_process(
    COMMAND "${ZP}" run src/main.zp
    WORKING_DIRECTORY "${consumer_dir}"
    RESULT_VARIABLE interpreter_result
    OUTPUT_VARIABLE interpreter_output
    ERROR_VARIABLE interpreter_error
    TIMEOUT 60
)
if(NOT interpreter_result EQUAL 0 OR
   NOT interpreter_output MATCHES "Greetings via transitive Z\\+ package")
    message(FATAL_ERROR
        "direct interpreter package import failed\n${interpreter_output}${interpreter_error}")
endif()
run_zpm(0 "${consumer_dir}" test)
if(NOT last_output MATCHES "1 passed, 0 failed")
    message(FATAL_ERROR "package tests did not pass through ZPBuild\n${last_output}")
endif()

file(READ "${consumer_dir}/zpm-lock.json" good_lock)
string(REPLACE "sha256:" "sha256:bad" corrupt_lock "${good_lock}")
file(WRITE "${consumer_dir}/zpm-lock.json" "${corrupt_lock}")
run_zpm(1 "${consumer_dir}" install --locked)
file(WRITE "${consumer_dir}/zpm-lock.json" "${good_lock}")

run_zpm(0 "${consumer_dir}" remove hello-package)
if(EXISTS "${consumer_dir}/z+_packages/hello-package" OR
   EXISTS "${consumer_dir}/z+_packages/greeting-core")
    message(FATAL_ERROR "removing the direct dependency left managed packages installed")
endif()
run_zpm(0 "${consumer_dir}" add "${package_dir}")
if(NOT EXISTS "${consumer_dir}/z+_packages/hello-package/src/main.zh" OR
   NOT EXISTS "${consumer_dir}/z+_packages/greeting-core/src/main.zh")
    message(FATAL_ERROR "local directory installation omitted package source")
endif()
run_zpbuild(0 "${consumer_dir}" run)
if(NOT last_output MATCHES "Greetings via transitive Z\\+ package")
    message(FATAL_ERROR "ZPBuild did not run the locally installed directory package")
endif()

execute_process(
    COMMAND "${ZPM}" init "offline-consumer"
    WORKING_DIRECTORY "${offline_dir}"
    RESULT_VARIABLE offline_init_result
    OUTPUT_VARIABLE offline_init_output
    ERROR_VARIABLE offline_init_error
    TIMEOUT 30
)
if(NOT offline_init_result EQUAL 0)
    message(FATAL_ERROR "offline consumer zpm init failed\n${offline_init_output}${offline_init_error}")
endif()
file(READ "${offline_dir}/zpm.json" offline_manifest)
string(REPLACE "\"dependencies\":{}"
    "\"dependencies\":{\"hello-package\":\"^0.1.0\"}"
    offline_manifest "${offline_manifest}")
file(WRITE "${offline_dir}/zpm.json" "${offline_manifest}")
run_zpm(0 "${offline_dir}" install)
if(NOT EXISTS "${offline_dir}/z+_packages/hello-package/src/main.zh" OR
   NOT EXISTS "${offline_dir}/z+_packages/greeting-core/src/main.zh")
    message(FATAL_ERROR "cached offline install omitted package source")
endif()

set(invalid_archive "${WORK_DIR}/invalid.zpm")
file(WRITE "${invalid_archive}" "not a ZPM archive")
run_zpm(1 "${consumer_dir}" install "${invalid_archive}")
