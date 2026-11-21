if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(valid_program "${SOURCE_DIR}/tests/classes/visibility.zp")
execute_process(
    COMMAND "${ZP}" check "${valid_program}"
    RESULT_VARIABLE check_status
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
    TIMEOUT 30
)
if(NOT check_status EQUAL 0)
    message(FATAL_ERROR "visibility fixture did not check\n${check_output}${check_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${valid_program}"
    RESULT_VARIABLE run_status
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
    TIMEOUT 30
)
if(NOT run_status EQUAL 0 OR NOT run_output STREQUAL "75\n4\n4\n")
    message(FATAL_ERROR "visibility interpreter result mismatch (${run_status})\n${run_output}${run_error}")
endif()

if(WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(native_executable "${WORK_DIR}/visibility-native")
    if(WIN32)
        set(native_executable "${native_executable}.exe")
    endif()
    execute_process(
        COMMAND "${ZP}" build "${valid_program}" -o "${native_executable}"
        RESULT_VARIABLE build_status
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 60
    )
    if(NOT build_status EQUAL 0)
        message(FATAL_ERROR "visibility fixture native build failed\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${native_executable}"
        RESULT_VARIABLE native_status
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 30
    )
    if(NOT native_status EQUAL 0 OR NOT native_output STREQUAL run_output)
        message(FATAL_ERROR "visibility native/interpreter mismatch (${native_status})\ninterpreter: ${run_output}\nnative: ${native_output}${native_error}")
    endif()
endif()

foreach(case IN ITEMS
    "invalid_private_field|private member 'value'"
    "invalid_private_assignment|private member 'value'"
    "invalid_private_method|private member 'reveal'"
    "invalid_protected_access|protected member 'value'"
    "invalid_duplicate_modifier|duplicate or conflicting class-member visibility modifier"
    "invalid_duplicate_member|duplicate class member 'value'")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 filename)
    list(GET parts 1 diagnostic)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/tests/classes/${filename}.zp"
        RESULT_VARIABLE invalid_status
        OUTPUT_VARIABLE invalid_output
        ERROR_VARIABLE invalid_error
        TIMEOUT 20
    )
    if(invalid_status EQUAL 0 OR NOT "${invalid_output}${invalid_error}" MATCHES "${diagnostic}")
        message(FATAL_ERROR "${filename} did not produce expected diagnostic [${diagnostic}] (${invalid_status})\n${invalid_output}${invalid_error}")
    endif()
endforeach()

foreach(command IN ITEMS run build)
    set(command_arguments "${command}" "${SOURCE_DIR}/tests/classes/invalid_private_field.zp")
    if(command STREQUAL "build")
        list(APPEND command_arguments -o "${WORK_DIR}/invalid-private.exe")
    endif()
    execute_process(
        COMMAND "${ZP}" ${command_arguments}
        RESULT_VARIABLE invalid_mode_status
        OUTPUT_VARIABLE invalid_mode_output
        ERROR_VARIABLE invalid_mode_error
        TIMEOUT 30
    )
    if(invalid_mode_status EQUAL 0 OR
       NOT "${invalid_mode_output}${invalid_mode_error}" MATCHES "private member 'value'")
        message(FATAL_ERROR "zp ${command} accepted private-member access (${invalid_mode_status})\n${invalid_mode_output}${invalid_mode_error}")
    endif()
endforeach()

set(module_dir "${WORK_DIR}/module")
file(MAKE_DIRECTORY "${module_dir}")
file(WRITE "${module_dir}/vault.zh"
    "start {\npub class Vault {\n    private secret: number = 12\n    pub fn value() -> number {\n        return this.secret\n    } end fn\n} end class\n} stop\n")
file(WRITE "${module_dir}/main.zp"
    "use vault.zh\nstart {\n    let vault = Vault()\n    say(vault.secret)\n} stop\n")
execute_process(
    COMMAND "${ZP}" check "${module_dir}/main.zp"
    RESULT_VARIABLE module_status
    OUTPUT_VARIABLE module_output
    ERROR_VARIABLE module_error
    TIMEOUT 20
)
if(module_status EQUAL 0 OR NOT "${module_output}${module_error}" MATCHES "private member 'secret'")
    message(FATAL_ERROR "cross-module private access was not rejected (${module_status})\n${module_output}${module_error}")
endif()

file(WRITE "${module_dir}/main.zp"
    "use vault.zh\nstart {\n    let vault = Vault()\n    say(vault.value())\n} stop\n")
execute_process(
    COMMAND "${ZP}" run "${module_dir}/main.zp"
    RESULT_VARIABLE module_valid_status
    OUTPUT_VARIABLE module_valid_output
    ERROR_VARIABLE module_valid_error
    TIMEOUT 30
)
if(NOT module_valid_status EQUAL 0 OR NOT module_valid_output STREQUAL "12\n")
    message(FATAL_ERROR "cross-module public member access failed (${module_valid_status})\n${module_valid_output}${module_valid_error}")
endif()

message(STATUS "Class visibility semantic and execution tests passed")
