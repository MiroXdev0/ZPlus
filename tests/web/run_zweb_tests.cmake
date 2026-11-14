if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/uploads")
file(REMOVE "${WORK_DIR}/uploads/zweb-smoke.bin")
set(source "${SOURCE_DIR}/tests/web/zweb_runtime_smoke.zp")
set(expected
    "a b/c\nexample.test\n/users\nq=one\nhttps://example.test/users?q=one\nJane Doe\nyes\n2\na b\nsession=a%20b; Max-Age=3600; Path=/; Secure; HttpOnly; SameSite=Lax\nshowUser\n42\n405\n404\nnosniff\ntext/css; charset=utf-8\ncreated\n")

execute_process(
    COMMAND "${ZP}" run "${source}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
    TIMEOUT 20
)
if(NOT run_result EQUAL 0 OR NOT "${run_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "ZWeb interpreter integration failed (${run_result})\nExpected: [${expected}]\nActual: [${run_output}]\n${run_error}")
endif()
file(REMOVE "${WORK_DIR}/uploads/zweb-smoke.bin")

set(features_source "${SOURCE_DIR}/tests/web/zweb_features_smoke.zp")
set(features_expected
    "user.show\ntrue\n/v1/users/a%20b\n201\ntrue\n{\"ok\":true}\n404\nGET, HEAD\ntrue\n37\none\n<h1>&lt;Ada&gt;</h1><ul><li>one</li><li>two</li></ul>\ntrue\nAda\ntrue\ntrue\ntrue\nfalse\nfalse\ntrue\ntrue\nsid=value; Max-Age=60; Path=/; Secure; HttpOnly; SameSite=Lax; Expires=Wed, 21 Oct 2015 07:28:00 GMT\ntrue\nhttps://example.test\n1\nhello\n1\ntrue\n")

execute_process(
    COMMAND "${ZP}" run "${features_source}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE features_result
    OUTPUT_VARIABLE features_output
    ERROR_VARIABLE features_error
    TIMEOUT 20
)
if(NOT features_result EQUAL 0 OR NOT "${features_output}" STREQUAL "${features_expected}")
    message(FATAL_ERROR "ZWeb feature interpreter integration failed (${features_result})\nExpected: [${features_expected}]\nActual: [${features_output}]\n${features_error}")
endif()
file(REMOVE "${WORK_DIR}/uploads/zweb-smoke.bin")

if(HOST_WINDOWS)
    set(executable "${WORK_DIR}/zweb_runtime_smoke.exe")
    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 30
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "ZWeb native build failed (${build_result})\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${executable}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 20
    )
    if(NOT native_result EQUAL 0 OR NOT "${native_output}" STREQUAL "${expected}")
        message(FATAL_ERROR "ZWeb native integration failed (${native_result})\nExpected: [${expected}]\nActual: [${native_output}]\n${native_error}")
    endif()
    file(REMOVE "${WORK_DIR}/uploads/zweb-smoke.bin")

    set(features_executable "${WORK_DIR}/zweb_features_smoke.exe")
    execute_process(
        COMMAND "${ZP}" build "${features_source}" -o "${features_executable}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE features_build_result
        OUTPUT_VARIABLE features_build_output
        ERROR_VARIABLE features_build_error
        TIMEOUT 30
    )
    if(NOT features_build_result EQUAL 0)
        message(FATAL_ERROR "ZWeb feature native build failed (${features_build_result})\n${features_build_output}${features_build_error}")
    endif()
    execute_process(
        COMMAND "${features_executable}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE features_native_result
        OUTPUT_VARIABLE features_native_output
        ERROR_VARIABLE features_native_error
        TIMEOUT 20
    )
    if(NOT features_native_result EQUAL 0 OR
        NOT "${features_native_output}" STREQUAL "${features_expected}")
        message(FATAL_ERROR "ZWeb feature native integration failed (${features_native_result})\nExpected: [${features_expected}]\nActual: [${features_native_output}]\n${features_native_error}")
    endif()
endif()

file(REMOVE "${WORK_DIR}/uploads/zweb-smoke.bin")
