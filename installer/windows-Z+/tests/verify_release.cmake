foreach(required IN ITEMS ZP ZBUILD SOURCE_FILE INSTALL_SOURCE PAYLOAD_DIRECTORY
                          MANIFEST_FILE WORK_DIR RUNTIME_FILE SQLITE_FILE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must be provided")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
foreach(runtime IN ITEMS "${RUNTIME_FILE}" "${SQLITE_FILE}")
    if(NOT EXISTS "${runtime}")
        message(FATAL_ERROR "Required compiler runtime is missing: ${runtime}")
    endif()
    file(COPY "${runtime}" DESTINATION "${WORK_DIR}")
endforeach()
get_filename_component(compiler_directory "${ZP}" DIRECTORY)
get_filename_component(zbuild_directory "${ZBUILD}" DIRECTORY)
if(NOT EXISTS "${compiler_directory}/stdlib/zstring/zstring.zh")
    message(FATAL_ERROR "The compiler's adjacent standard library is missing")
endif()
if(NOT IS_DIRECTORY "${zbuild_directory}/zbuild-templates")
    message(FATAL_ERROR "The ZBuild template collection is missing")
endif()
foreach(runtime IN ITEMS
        zgraphics_runtime.dll zaudio_runtime.dll zphysics_runtime.dll sqlite3.dll)
    if(NOT EXISTS "${PAYLOAD_DIRECTORY}/bin/${runtime}")
        message(FATAL_ERROR "The installer payload is missing ${runtime}")
    endif()
endforeach()
foreach(notice IN ITEMS Box2D-LICENSE.txt miniaudio-LICENSE.txt SQLite-LICENSE.txt
                        stb_vorbis-NOTICE.txt)
    if(NOT EXISTS "${PAYLOAD_DIRECTORY}/THIRD-PARTY-NOTICES/${notice}")
        message(FATAL_ERROR "The installer payload is missing third-party notice ${notice}")
    endif()
endforeach()

execute_process(
    COMMAND "${ZP}" version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
    TIMEOUT 20
)
if(NOT version_result EQUAL 0 OR NOT "${version_output}" MATCHES "1\\.1\\.2")
    message(FATAL_ERROR "Packaged compiler version check failed\n${version_output}${version_error}")
endif()

execute_process(
    COMMAND "${ZBUILD}" --version
    RESULT_VARIABLE zbuild_result
    OUTPUT_VARIABLE zbuild_output
    ERROR_VARIABLE zbuild_error
    TIMEOUT 20
)
if(NOT zbuild_result EQUAL 0 OR NOT "${zbuild_output}" MATCHES "ZBuild")
    message(FATAL_ERROR "Packaged ZBuild version check failed\n${zbuild_output}${zbuild_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${SOURCE_FILE}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE interpreted_result
    OUTPUT_VARIABLE interpreted_output
    ERROR_VARIABLE interpreted_error
    TIMEOUT 60
)
if(NOT interpreted_result EQUAL 0 OR
   NOT "${interpreted_output}" MATCHES "ZPLUS_RELEASE_VERIFY_OK")
    message(FATAL_ERROR
        "Packaged compiler interpreter verification failed\n"
        "${interpreted_output}${interpreted_error}")
endif()

if(WIN32)
    set(native "${WORK_DIR}/release-verification.exe")
else()
    set(native "${WORK_DIR}/release-verification")
endif()
execute_process(
    COMMAND "${ZP}" build "${SOURCE_FILE}" -o "${native}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
    TIMEOUT 60
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "Packaged compiler native build failed\n${build_output}${build_error}")
endif()
execute_process(
    COMMAND "${native}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_output
    ERROR_VARIABLE native_error
    TIMEOUT 60
)
if(NOT native_result EQUAL 0 OR
   NOT "${native_output}" MATCHES "ZPLUS_RELEASE_VERIFY_OK")
    message(FATAL_ERROR
        "Packaged compiler native execution failed\n${native_output}${native_error}")
endif()

set(install_source "${WORK_DIR}/release-install-workflow.exe")
execute_process(
    COMMAND "${ZP}" build "${INSTALL_SOURCE}" -o "${install_source}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE install_build_result
    OUTPUT_VARIABLE install_build_output
    ERROR_VARIABLE install_build_error
    TIMEOUT 60
)
if(NOT install_build_result EQUAL 0)
    message(FATAL_ERROR
        "Packaged installer workflow did not compile\n"
        "${install_build_output}${install_build_error}")
endif()
set(interpreted_install_work "${WORK_DIR}/install-target-interpreter")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "ZPLUS_TEST_PAYLOAD=${PAYLOAD_DIRECTORY}"
        "ZPLUS_TEST_MANIFEST=${MANIFEST_FILE}"
        "ZPLUS_TEST_INSTALL_ROOT=${interpreted_install_work}"
        "${ZP}" run "${INSTALL_SOURCE}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE install_interpreter_result
    OUTPUT_VARIABLE install_interpreter_output
    ERROR_VARIABLE install_interpreter_error
    TIMEOUT 120
)
if(NOT install_interpreter_result EQUAL 0 OR
   NOT "${install_interpreter_output}" MATCHES "ZPLUS_RELEASE_INSTALL_WORKFLOW_OK")
    message(FATAL_ERROR
        "Packaged compiler could not run the install/uninstall workflow\n"
        "${install_interpreter_output}${install_interpreter_error}")
endif()

set(native_install_work "${WORK_DIR}/install-target-native")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "ZPLUS_TEST_PAYLOAD=${PAYLOAD_DIRECTORY}"
        "ZPLUS_TEST_MANIFEST=${MANIFEST_FILE}"
        "ZPLUS_TEST_INSTALL_ROOT=${native_install_work}"
        "${install_source}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE install_native_result
    OUTPUT_VARIABLE install_native_output
    ERROR_VARIABLE install_native_error
    TIMEOUT 120
)
if(NOT install_native_result EQUAL 0 OR
   NOT "${install_native_output}" MATCHES "ZPLUS_RELEASE_INSTALL_WORKFLOW_OK")
    message(FATAL_ERROR
        "Packaged native installer install/uninstall workflow failed\n"
        "${install_native_output}${install_native_error}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
