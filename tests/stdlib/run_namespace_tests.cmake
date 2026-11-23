if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(canonical_source "${WORK_DIR}/canonical.zp")
file(WRITE "${canonical_source}"
    "use zp.memory.management\nstart {\n    say(memory.heapObjects() >= 0)\n} stop\n")

execute_process(
    COMMAND "${ZP}" check "${canonical_source}"
    RESULT_VARIABLE canonical_check_result
    OUTPUT_VARIABLE canonical_check_output
    ERROR_VARIABLE canonical_check_error
    TIMEOUT 20
)
if(NOT canonical_check_result EQUAL 0 OR
   NOT "${canonical_check_error}" STREQUAL "")
    message(FATAL_ERROR
        "canonical nested namespace check failed (${canonical_check_result})\n"
        "${canonical_check_output}${canonical_check_error}")
endif()

execute_process(
    COMMAND "${ZP}" dependencies "${canonical_source}"
    RESULT_VARIABLE dependencies_result
    OUTPUT_VARIABLE dependencies_output
    ERROR_VARIABLE dependencies_error
    TIMEOUT 20
)
string(REPLACE "\\\\" "/" dependencies_normalized "${dependencies_output}")
if(NOT dependencies_result EQUAL 0 OR
   NOT "${dependencies_normalized}" MATCHES "stdlib/zp/memory/management\\.zh")
    message(FATAL_ERROR
        "canonical namespace dependency was not discovered (${dependencies_result})\n"
        "${dependencies_output}${dependencies_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${canonical_source}"
    RESULT_VARIABLE canonical_run_result
    OUTPUT_VARIABLE canonical_run_output
    ERROR_VARIABLE canonical_run_error
    TIMEOUT 20
)
if(NOT canonical_run_result EQUAL 0 OR
   NOT "${canonical_run_output}" STREQUAL "true\n")
    message(FATAL_ERROR
        "canonical namespace interpreter run failed (${canonical_run_result})\n"
        "${canonical_run_output}${canonical_run_error}")
endif()

set(missing_source "${WORK_DIR}/missing.zp")
file(WRITE "${missing_source}" "use zp.no_such_library\nstart { } stop\n")
execute_process(
    COMMAND "${ZP}" check "${missing_source}"
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_output
    ERROR_VARIABLE missing_error
    TIMEOUT 20
)
if(missing_result EQUAL 0 OR
   NOT "${missing_output}${missing_error}" MATCHES "module 'zp\\.no_such_library' was not found")
    message(FATAL_ERROR
        "missing canonical library did not produce the expected diagnostic\n"
        "${missing_output}${missing_error}")
endif()

set(invalid_source "${WORK_DIR}/invalid.zp")
file(WRITE "${invalid_source}" "use zp..string\nstart { } stop\n")
execute_process(
    COMMAND "${ZP}" check "${invalid_source}"
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_output
    ERROR_VARIABLE invalid_error
    TIMEOUT 20
)
if(invalid_result EQUAL 0 OR
   NOT "${invalid_output}${invalid_error}" MATCHES "invalid standard-library namespace")
    message(FATAL_ERROR
        "invalid canonical namespace did not produce the expected diagnostic\n"
        "${invalid_output}${invalid_error}")
endif()

set(legacy_source "${WORK_DIR}/legacy.zp")
file(WRITE "${legacy_source}"
    "use zmath\nstart {\n    say(zmath.sqrt(9))\n} stop\n")
execute_process(
    COMMAND "${ZP}" run "${legacy_source}"
    RESULT_VARIABLE legacy_result
    OUTPUT_VARIABLE legacy_output
    ERROR_VARIABLE legacy_error
    TIMEOUT 20
)
if(NOT legacy_result EQUAL 0 OR
   NOT "${legacy_output}" STREQUAL "3\n" OR
   NOT "${legacy_error}" MATCHES "deprecated; use 'zp\\.math'")
    message(FATAL_ERROR
        "legacy standard-library compatibility failed (${legacy_result})\n"
        "${legacy_output}${legacy_error}")
endif()

set(legacy_math_source "${WORK_DIR}/legacy-math.zp")
file(WRITE "${legacy_math_source}"
    "use math as legacyMath\nstart {\n    say(legacyMath.abs(-7))\n} stop\n")
execute_process(
    COMMAND "${ZP}" run "${legacy_math_source}"
    RESULT_VARIABLE legacy_math_result
    OUTPUT_VARIABLE legacy_math_output
    ERROR_VARIABLE legacy_math_error
    TIMEOUT 20
)
if(NOT legacy_math_result EQUAL 0 OR
   NOT "${legacy_math_output}" STREQUAL "7\n" OR
   NOT "${legacy_math_error}" MATCHES "deprecated; use 'zp\\.math\\.legacy'")
    message(FATAL_ERROR
        "legacy math helper compatibility failed (${legacy_math_result})\n"
        "${legacy_math_output}${legacy_math_error}")
endif()

set(native_source "${WORK_DIR}/canonical-native.zp")
file(WRITE "${native_source}"
    "use zp.math.legacy as legacyMath\nstart {\n    say(legacyMath.abs(-7))\n} stop\n")
if(HOST_WINDOWS)
    set(native_output "${WORK_DIR}/canonical-native.exe")
else()
    set(native_output "${WORK_DIR}/canonical-native")
endif()
execute_process(
    COMMAND "${ZP}" build "${native_source}" -o "${native_output}"
    RESULT_VARIABLE native_build_result
    OUTPUT_VARIABLE native_build_output
    ERROR_VARIABLE native_build_error
    TIMEOUT 30
)
if(NOT native_build_result EQUAL 0)
    message(FATAL_ERROR
        "canonical namespace native build failed (${native_build_result})\n"
        "${native_build_output}${native_build_error}")
endif()
execute_process(
    COMMAND "${native_output}"
    RESULT_VARIABLE native_run_result
    OUTPUT_VARIABLE native_run_output
    ERROR_VARIABLE native_run_error
    TIMEOUT 20
)
if(NOT native_run_result EQUAL 0 OR
   NOT "${native_run_output}" STREQUAL "7\n")
    message(FATAL_ERROR
        "canonical namespace native run failed (${native_run_result})\n"
        "${native_run_output}${native_run_error}")
endif()
