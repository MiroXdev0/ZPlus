if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(source "${SOURCE_DIR}/tests/parser/control_numeric_precedence.zp")
set(expected "binary-first-ok\nbinary-second-ok\nbinary-add-ok\nhexadecimal-ok\ndecimal-ok\nscientific-ok\nif-without-else\nnested-if\nempty-then-branch\nbitwise-precedence-ok\ngrouped-bitwise-ok\n")

execute_process(
    COMMAND "${ZP}" check "${source}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
    TIMEOUT 20
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "compiler check failed (${check_result})\n${check_output}${check_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${source}"
    RESULT_VARIABLE interpreter_result
    OUTPUT_VARIABLE interpreter_output
    ERROR_VARIABLE interpreter_error
    TIMEOUT 20
)
if(NOT interpreter_result EQUAL 0 OR NOT "${interpreter_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "interpreter mismatch (${interpreter_result})\nexpected:\n${expected}\nactual:\n${interpreter_output}${interpreter_error}")
endif()

set(executable "${WORK_DIR}/control_numeric_precedence.native")
execute_process(
    COMMAND "${ZP}" build "${source}" -o "${executable}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
    TIMEOUT 30
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "native build failed (${build_result})\n${build_output}${build_error}")
endif()

execute_process(
    COMMAND "${executable}"
    RESULT_VARIABLE native_result
    OUTPUT_VARIABLE native_output
    ERROR_VARIABLE native_error
    TIMEOUT 20
)
if(NOT native_result EQUAL 0 OR NOT "${native_output}" STREQUAL "${expected}")
    message(FATAL_ERROR "native parity mismatch (${native_result})\nexpected:\n${expected}\nactual:\n${native_output}${native_error}")
endif()

foreach(case unsupported_is malformed_binary_literal invalid_escape expected_token switch_invalid_case_terminator struct_recovery_no_progress enum_recovery_no_progress legacy_switch_recovery_no_progress)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/tests/parser/${case}.zp"
        RESULT_VARIABLE invalid_result
        OUTPUT_VARIABLE invalid_output
        ERROR_VARIABLE invalid_error
        TIMEOUT 20
    )
    if(invalid_result EQUAL 0)
        message(FATAL_ERROR "${case}: unsupported or malformed source unexpectedly passed")
    endif()
    if(case STREQUAL "unsupported_is" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "right operand of 'is' must be a declared type name")
        message(FATAL_ERROR "unsupported_is: missing explicit type-name diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "malformed_binary_literal" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "invalid character in binary numeric literal")
        message(FATAL_ERROR "malformed_binary_literal: missing numeric-literal diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "invalid_escape" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "unknown escape sequence")
        message(FATAL_ERROR "invalid_escape: missing lexer diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "switch_invalid_case_terminator" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "expected 'case', 'default', or '\}' in switch")
        message(FATAL_ERROR "switch_invalid_case_terminator: missing switch diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "struct_recovery_no_progress" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "expected struct field name")
        message(FATAL_ERROR "struct_recovery_no_progress: missing struct diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "enum_recovery_no_progress" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "expected enum member")
        message(FATAL_ERROR "enum_recovery_no_progress: missing enum diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "legacy_switch_recovery_no_progress" AND
       NOT "${invalid_output}${invalid_error}" MATCHES "expected 'case', 'default', or 'end' in switch")
        message(FATAL_ERROR "legacy_switch_recovery_no_progress: missing switch diagnostic\n${invalid_output}${invalid_error}")
    endif()
    if(case STREQUAL "expected_token")
        set(diagnostic "${invalid_output}${invalid_error}")
        if(NOT diagnostic MATCHES "found.*RightBracket")
            message(FATAL_ERROR "expected_token: missing unexpected-token detail\n${diagnostic}")
        endif()
        if(NOT diagnostic MATCHES "expected_token\\.zp:[0-9]+:[0-9]+")
            message(FATAL_ERROR "expected_token: missing source location\n${diagnostic}")
        endif()
        if(NOT diagnostic MATCHES "let value = \\(1 \\+ 2\\]")
            message(FATAL_ERROR "expected_token: missing source excerpt\n${diagnostic}")
        endif()

        execute_process(
            COMMAND "${ZP}" check "${SOURCE_DIR}/tests/parser/expected_token.zp" --diagnostics=json
            RESULT_VARIABLE json_result
            OUTPUT_VARIABLE json_output
            ERROR_VARIABLE json_error
            TIMEOUT 20
        )
        if(json_result EQUAL 0 OR
           NOT "${json_output}${json_error}" MATCHES "\"code\":\"E1001\"" OR
           NOT "${json_output}${json_error}" MATCHES "\"category\":\"Syntax error\"" OR
           NOT "${json_output}${json_error}" MATCHES "\"sourceLine\":\".*let value.*\"" OR
           NOT "${json_output}${json_error}" MATCHES "\"suggestion\":\"Replace")
            message(FATAL_ERROR "expected_token: missing structured syntax diagnostic\n${json_output}${json_error}")
        endif()
        string(REGEX MATCHALL "\"code\":\"E1001\"" syntax_diagnostics
            "${json_output}${json_error}")
        list(LENGTH syntax_diagnostics syntax_diagnostic_count)
        if(NOT syntax_diagnostic_count EQUAL 1)
            message(FATAL_ERROR "expected_token: parser emitted duplicate diagnostics for one token\n${json_output}${json_error}")
        endif()
    endif()
endforeach()

execute_process(
    COMMAND "${ZP}" dependencies
        "${SOURCE_DIR}/tests/parser/switch_invalid_case_terminator.zp"
    RESULT_VARIABLE switch_dependencies_result
    OUTPUT_VARIABLE switch_dependencies_output
    ERROR_VARIABLE switch_dependencies_error
    TIMEOUT 20
)
if(switch_dependencies_result EQUAL 0 OR
   NOT "${switch_dependencies_output}${switch_dependencies_error}"
       MATCHES "expected 'case', 'default', or '}' in switch")
    message(FATAL_ERROR
        "switch_invalid_case_terminator: dependencies did not terminate with a diagnostic\n"
        "${switch_dependencies_output}${switch_dependencies_error}")
endif()

execute_process(
    COMMAND "${ZP}" run
        "${SOURCE_DIR}/tests/parser/module_class_pub_method/main.zp"
    RESULT_VARIABLE class_dependencies_result
    OUTPUT_VARIABLE class_dependencies_output
    ERROR_VARIABLE class_dependencies_error
    TIMEOUT 20
)
if(NOT class_dependencies_result EQUAL 0 OR
   NOT "${class_dependencies_output}" STREQUAL "Hello, Z+\n")
    message(FATAL_ERROR
        "class_pub_method: imported public class method failed\n"
        "${class_dependencies_output}${class_dependencies_error}")
endif()

execute_process(
    COMMAND "${ZP}" check
        "${SOURCE_DIR}/tests/parser/unicode_position.zp"
        --diagnostics=json
    RESULT_VARIABLE unicode_position_status
    OUTPUT_VARIABLE unicode_position_output
    ERROR_VARIABLE unicode_position_error
    TIMEOUT 20
)
if(unicode_position_status EQUAL 0 OR
   NOT "${unicode_position_output}${unicode_position_error}" MATCHES
       "\"line\":2,\"column\":15")
    message(FATAL_ERROR
        "unicode_position: diagnostic column is not UTF-16-compatible\n"
        "${unicode_position_output}${unicode_position_error}")
endif()
