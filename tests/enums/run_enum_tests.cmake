if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(valid_program "${SOURCE_DIR}/tests/enums/values.zp")
set(expected "true\ntrue\ntrue\n5\n-2\n5\n16\n17\n")

execute_process(
    COMMAND "${ZP}" check "${valid_program}"
    RESULT_VARIABLE check_status
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
    TIMEOUT 30
)
if(NOT check_status EQUAL 0)
    message(FATAL_ERROR "enum values fixture failed checking\n${check_output}${check_error}")
endif()
execute_process(
    COMMAND "${ZP}" run "${valid_program}"
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
    TIMEOUT 30
)
if(NOT run_status EQUAL 0 OR NOT run_output STREQUAL expected)
    message(FATAL_ERROR "enum interpreter output mismatch (${run_status})\n${run_output}${run_error}")
endif()

if(WIN32)
    set(native_executable "${WORK_DIR}/enum-values")
    set(native_executable "${native_executable}.exe")
    execute_process(
        COMMAND "${ZP}" build "${valid_program}" -o "${native_executable}"
        RESULT_VARIABLE build_status
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 60
    )
    if(NOT build_status EQUAL 0)
        message(FATAL_ERROR "enum native build failed\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${native_executable}"
        RESULT_VARIABLE native_status
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 30
    )
    if(NOT native_status EQUAL 0 OR NOT native_output STREQUAL expected)
        message(FATAL_ERROR "enum interpreter/native mismatch (${native_status})\ninterpreter: ${run_output}\nnative: ${native_output}${native_error}")
    endif()
endif()

foreach(case IN ITEMS
    "duplicate_member|duplicate enum member 'Ready'"
    "fractional_value|enum values must be integer literals"
    "fractional_spelling|enum values must be integer literals"
    "computed_value|enum values must be integer literals"
    "value_out_of_range|enum values must be integer literals"
    "auto_value_out_of_range|automatic enum value exceeds"
    "invalid_assignment|cannot initialize 'numericValue' with type 'State'; expected 'number'"
    "invalid_member|enum 'State' has no member 'Missing'"
    "incompatible_comparison|cannot compare 'First' with 'Second'")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 filename)
    list(GET parts 1 diagnostic)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/tests/enums/${filename}.zp"
        RESULT_VARIABLE invalid_status
        OUTPUT_VARIABLE invalid_output
        ERROR_VARIABLE invalid_error
        TIMEOUT 20
    )
    if(invalid_status EQUAL 0 OR
       NOT "${invalid_output}${invalid_error}" MATCHES "${diagnostic}")
        message(FATAL_ERROR "${filename} missed diagnostic [${diagnostic}] (${invalid_status})\n${invalid_output}${invalid_error}")
    endif()
endforeach()

set(module_dir "${WORK_DIR}/modules")
file(MAKE_DIRECTORY "${module_dir}")
file(WRITE "${module_dir}/states.zh"
    "start {\npub enum PublicState {\n    Waiting,\n    Running = 6,\n    Done\n} end enum\nenum HiddenState {\n    Secret\n} end enum\npub fn current() -> PublicState {\n    return PublicState.Running\n} end fn\n} stop\n")
file(WRITE "${module_dir}/main.zp"
    "use states.zh as states\nstart {\n    let value: PublicState = states.PublicState.Running\n    say(value == current())\n    say(states.PublicState.Done)\n} stop\n")
execute_process(
    COMMAND "${ZP}" run "${module_dir}/main.zp"
    RESULT_VARIABLE module_status
    OUTPUT_VARIABLE module_output
    ERROR_VARIABLE module_error
    TIMEOUT 30
)
if(NOT module_status EQUAL 0 OR NOT module_output STREQUAL "true\n7\n")
    message(FATAL_ERROR "public imported enum failed (${module_status})\n${module_output}${module_error}")
endif()

if(WIN32)
    set(module_native "${module_dir}/enum-module")
    set(module_native "${module_native}.exe")
    execute_process(
        COMMAND "${ZP}" build "${module_dir}/main.zp" -o "${module_native}"
        RESULT_VARIABLE module_build_status
        OUTPUT_VARIABLE module_build_output
        ERROR_VARIABLE module_build_error
        TIMEOUT 60
    )
    if(NOT module_build_status EQUAL 0)
        message(FATAL_ERROR "native imported enum build failed\n${module_build_output}${module_build_error}")
    endif()
    execute_process(
        COMMAND "${module_native}"
        RESULT_VARIABLE module_native_status
        OUTPUT_VARIABLE module_native_output
        ERROR_VARIABLE module_native_error
        TIMEOUT 30
    )
    if(NOT module_native_status EQUAL 0 OR NOT module_native_output STREQUAL module_output)
        message(FATAL_ERROR "native imported enum behavior differs (${module_native_status})\n${module_native_output}${module_native_error}")
    endif()
endif()

file(WRITE "${module_dir}/private.zp"
    "use states.zh as states\nstart {\n    let value = states.HiddenState.Secret\n} stop\n")
execute_process(
    COMMAND "${ZP}" check "${module_dir}/private.zp"
    RESULT_VARIABLE private_status
    OUTPUT_VARIABLE private_output
    ERROR_VARIABLE private_error
    TIMEOUT 20
)
if(private_status EQUAL 0 OR
   NOT "${private_output}${private_error}" MATCHES "type 'HiddenState' is private to module 'states'")
    message(FATAL_ERROR "qualified private enum was accessible (${private_status})\n${private_output}${private_error}")
endif()

message(STATUS "Enum support semantic and backend tests passed")
