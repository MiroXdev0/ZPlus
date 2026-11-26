if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(source "${SOURCE_DIR}/tests/parser/brace_syntax.zp")
set(expected "hello Z+\nneeds improvement\n0\n1\n2\n8\nswitch passed\n")

execute_process(
    COMMAND "${ZP}" check "${source}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
    TIMEOUT 20
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "brace syntax check failed\n${check_output}${check_error}")
endif()

execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/parser/brace_declarations.zp"
    RESULT_VARIABLE declaration_result
    OUTPUT_VARIABLE declaration_output
    ERROR_VARIABLE declaration_error
    TIMEOUT 20
)
if(NOT declaration_result EQUAL 0)
    message(FATAL_ERROR "braced declarations check failed\n${declaration_output}${declaration_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/parser/declaration_structure.zp"
    RESULT_VARIABLE declaration_structure_result
    OUTPUT_VARIABLE declaration_structure_output
    ERROR_VARIABLE declaration_structure_error
    TIMEOUT 20
)
if(NOT declaration_structure_result EQUAL 0 OR
   NOT "${declaration_structure_output}" STREQUAL "2\n7\n")
    message(FATAL_ERROR "declarations inside start failed\n${declaration_structure_output}${declaration_structure_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${SOURCE_DIR}/tests/parser/legacy_syntax.zp"
    RESULT_VARIABLE legacy_result
    OUTPUT_VARIABLE legacy_output
    ERROR_VARIABLE legacy_error
    TIMEOUT 20
)
if(NOT legacy_result EQUAL 0 OR NOT "${legacy_output}" STREQUAL "legacy compatibility\n")
    message(FATAL_ERROR "legacy syntax compatibility failed\n${legacy_output}${legacy_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${source}"
    RESULT_VARIABLE interpreter_result
    OUTPUT_VARIABLE interpreter_output
    ERROR_VARIABLE interpreter_error
    TIMEOUT 20
)
if(NOT interpreter_result EQUAL 0 OR NOT "${interpreter_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "brace syntax interpreter mismatch\nexpected:\n${expected}\nactual:\n${interpreter_output}${interpreter_error}")
endif()

if(WIN32)
set(executable "${WORK_DIR}/brace_syntax.native.exe")
execute_process(
    COMMAND "${ZP}" build "${source}" -o "${executable}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
    TIMEOUT 30
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "brace syntax native build failed\n${build_output}${build_error}")
endif()

execute_process(
    COMMAND "${executable}"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_output
    ERROR_VARIABLE native_error
    TIMEOUT 20
)
if(NOT native_result EQUAL 0 OR NOT "${native_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "brace syntax native mismatch\nexpected:\n${expected}\nactual:\n${native_output}${native_error}")
endif()
endif()

set(invalid_cases
    "brace_mismatched_terminator|closed with 'end while'"
    "brace_missing_close|expected '}' to close 'if' block"
    "brace_missing_terminator|expected 'stop' to close 'start'"
    "brace_unexpected_close|unexpected closing brace"
    "brace_mixed_syntax|cannot mix legacy colon-based block syntax"
    "brace_unexpected_terminator|unexpected block terminator 'end while'"
    "brace_duplicate_terminator|unexpected block terminator"
    "brace_invalid_nesting|block opened as 'while'.*closed with 'end if'"
    "brace_missing_fn_terminator|expected 'end fn'"
    "brace_missing_loop_terminator|expected 'end while'"
    "function_outside_start|fn.*declarations must be inside the program's start block"
    "namespace_outside_start|namespace.*declarations must be inside the program's start block"
    "class_outside_start|class.*declarations must be inside the program's start block"
    "enum_outside_start|enum.*declarations must be inside the program's start block"
    "struct_outside_start|struct.*declarations must be inside the program's start block"
    "nested_declaration|fn.*declarations may only appear directly inside"
    "duplicate_declaration|duplicate declaration 'duplicate'"
)
foreach(case IN LISTS invalid_cases)
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 expected_message)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/tests/parser/${name}.zp"
        RESULT_VARIABLE invalid_result
        OUTPUT_VARIABLE invalid_output
        ERROR_VARIABLE invalid_error
        TIMEOUT 20
    )
    if(invalid_result EQUAL 0)
        message(FATAL_ERROR "${name}: malformed source unexpectedly passed")
    endif()
    if(NOT "${invalid_output}${invalid_error}" MATCHES "${expected_message}")
        message(FATAL_ERROR "${name}: diagnostic did not contain '${expected_message}'\n${invalid_output}${invalid_error}")
    endif()
endforeach()
