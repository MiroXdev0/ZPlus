if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_case name expected_exit expected_output)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    set(executable "${WORK_DIR}/${name}.native")

    if(NOT HOST_WINDOWS)
        return()
    endif()
    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
        TIMEOUT 15
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "${name}: native build failed (${build_result})\n${build_stdout}${build_stderr}")
    endif()

    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
        TIMEOUT 10
    )
    if(NOT run_result EQUAL expected_exit)
        message(FATAL_ERROR "${name}: expected exit ${expected_exit}, got ${run_result}\n${run_stdout}${run_stderr}")
    endif()
    if(NOT "${run_stdout}" STREQUAL "${expected_output}")
        message(FATAL_ERROR "${name}: unexpected output\nexpected: [${expected_output}]\nactual: [${run_stdout}]")
    endif()
endfunction()

function(run_interpreter_case name expected_output)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    execute_process(
        COMMAND "${ZP}" run "${source}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
        TIMEOUT 10
    )
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR "${name}: interpreter run failed (${run_result})\n${run_stdout}${run_stderr}")
    endif()
    if(NOT "${run_stdout}" STREQUAL "${expected_output}")
        message(FATAL_ERROR "${name}: unexpected interpreter output\nexpected: [${expected_output}]\nactual: [${run_stdout}]")
    endif()
endfunction()

function(run_native_interpreter_parity name expected_output)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    set(executable "${WORK_DIR}/${name}.native")

    execute_process(
        COMMAND "${ZP}" run "${source}"
        RESULT_VARIABLE interpreter_result
        OUTPUT_VARIABLE interpreter_output
        ERROR_VARIABLE interpreter_error
        TIMEOUT 15
    )
    if(NOT interpreter_result EQUAL 0 OR
       NOT "${interpreter_output}" STREQUAL "${expected_output}")
        message(FATAL_ERROR
            "${name}: interpreter result mismatch (${interpreter_result})\n"
            "expected: [${expected_output}]\nactual: [${interpreter_output}]\n"
            "${interpreter_error}")
    endif()

    if(NOT HOST_WINDOWS)
        return()
    endif()

    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR
            "${name}: native build failed (${build_result})\n"
            "${build_stdout}${build_stderr}")
    endif()

    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 15
    )
    if(NOT native_result EQUAL 0 OR
       NOT "${native_output}" STREQUAL "${interpreter_output}")
        message(FATAL_ERROR
            "${name}: interpreter/native result mismatch (${native_result})\n"
            "interpreter: [${interpreter_output}]\n"
            "native: [${native_output}]\n${native_error}")
    endif()
endfunction()

function(run_native_runtime_error name expected_message)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    set(executable "${WORK_DIR}/${name}.native")
    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
        TIMEOUT 15
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "${name}: native build failed (${build_result})\n${build_stdout}${build_stderr}")
    endif()
    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_stdout
        ERROR_VARIABLE run_stderr
        TIMEOUT 10
    )
    if(run_result EQUAL 0 OR
       NOT "${run_stdout}${run_stderr}" MATCHES "${expected_message}")
        message(FATAL_ERROR "${name}: expected native runtime error [${expected_message}]\n${run_stdout}${run_stderr}")
    endif()
endfunction()

function(run_native_build_error name expected_message)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${WORK_DIR}/${name}.native"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
        TIMEOUT 15
    )
    if(build_result EQUAL 0 OR
       NOT "${build_stdout}${build_stderr}" MATCHES "${expected_message}" OR
       NOT "${build_stdout}${build_stderr}" MATCHES
           "E5001.*Backend error")
        message(FATAL_ERROR "${name}: expected native build error [${expected_message}]\n${build_stdout}${build_stderr}")
    endif()
endfunction()

function(run_check_failure name expected_message)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    execute_process(
        COMMAND "${ZP}" check "${source}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_stdout
        ERROR_VARIABLE check_stderr
        TIMEOUT 10
    )
    if(check_result EQUAL 0 OR
       NOT "${check_stdout}${check_stderr}" MATCHES "${expected_message}")
        message(FATAL_ERROR "${name}: expected check failure containing [${expected_message}]\n${check_stdout}${check_stderr}")
    endif()
endfunction()

function(run_input_case name input_text expected_output)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    set(input_file "${WORK_DIR}/${name}.stdin")
    set(executable "${WORK_DIR}/${name}.native")
    file(WRITE "${input_file}" "${input_text}")

    execute_process(
        COMMAND "${ZP}" run "${source}"
        INPUT_FILE "${input_file}"
        RESULT_VARIABLE interpreter_result
        OUTPUT_VARIABLE interpreter_output
        ERROR_VARIABLE interpreter_error
        TIMEOUT 15
    )
    if(NOT interpreter_result EQUAL 0 OR
       NOT "${interpreter_output}" STREQUAL "${expected_output}")
        message(FATAL_ERROR
            "${name}: interpreter input mismatch (${interpreter_result})\n"
            "expected: [${expected_output}]\nactual: [${interpreter_output}]\n"
            "${interpreter_error}")
    endif()

    if(NOT HOST_WINDOWS)
        return()
    endif()

    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR
            "${name}: native build failed (${build_result})\n${build_stdout}${build_stderr}")
    endif()

    execute_process(
        COMMAND "${executable}"
        INPUT_FILE "${input_file}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 15
    )
    if(NOT native_result EQUAL 0 OR
       NOT "${native_output}" STREQUAL "${expected_output}")
        message(FATAL_ERROR
            "${name}: native input mismatch (${native_result})\n"
            "expected: [${expected_output}]\nactual: [${native_output}]\n"
            "${native_error}")
    endif()
    if(NOT "${native_output}" STREQUAL "${interpreter_output}")
        message(FATAL_ERROR "${name}: interpreter/native input output differs")
    endif()
endfunction()

function(run_interpreter_input_case name input_text expected_output)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    set(input_file "${WORK_DIR}/${name}.stdin")
    file(WRITE "${input_file}" "${input_text}")
    execute_process(
        COMMAND "${ZP}" run "${source}"
        INPUT_FILE "${input_file}"
        RESULT_VARIABLE run_result
        OUTPUT_VARIABLE run_output
        ERROR_VARIABLE run_error
        TIMEOUT 15
    )
    if(NOT run_result EQUAL 0 OR NOT "${run_output}" STREQUAL "${expected_output}")
        message(FATAL_ERROR
            "${name}: interpreter input mismatch (${run_result})\n"
            "expected: [${expected_output}]\nactual: [${run_output}]\n${run_error}")
    endif()
endfunction()

function(run_input_failure name input_text expected_message)
    set(source "${SOURCE_DIR}/tests/native/${name}.zp")
    set(input_file "${WORK_DIR}/${name}.stdin")
    set(executable "${WORK_DIR}/${name}.native")
    file(WRITE "${input_file}" "${input_text}")

    execute_process(
        COMMAND "${ZP}" run "${source}"
        INPUT_FILE "${input_file}"
        RESULT_VARIABLE interpreter_result
        OUTPUT_VARIABLE interpreter_output
        ERROR_VARIABLE interpreter_error
        TIMEOUT 15
    )
    if(interpreter_result EQUAL 0 OR
       NOT "${interpreter_output}${interpreter_error}" MATCHES "${expected_message}")
        message(FATAL_ERROR
            "${name}: interpreter did not report [${expected_message}]\n"
            "${interpreter_output}${interpreter_error}")
    endif()

    if(NOT HOST_WINDOWS)
        return()
    endif()

    execute_process(
        COMMAND "${ZP}" build "${source}" -o "${executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_stdout
        ERROR_VARIABLE build_stderr
        TIMEOUT 20
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR
            "${name}: native build failed (${build_result})\n${build_stdout}${build_stderr}")
    endif()

    execute_process(
        COMMAND "${executable}"
        INPUT_FILE "${input_file}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 15
    )
    if(native_result EQUAL 0 OR
       NOT "${native_output}${native_error}" MATCHES "${expected_message}")
        message(FATAL_ERROR
            "${name}: native did not report [${expected_message}]\n"
            "${native_output}${native_error}")
    endif()
endfunction()

function(run_input_arity_check)
    set(source "${SOURCE_DIR}/tests/native/terminal_input_bad_arity.zp")
    execute_process(
        COMMAND "${ZP}" check "${source}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_output
        ERROR_VARIABLE check_error
        TIMEOUT 15
    )
    string(REGEX MATCHALL "too many arguments" arity_errors
        "${check_output}${check_error}")
    list(LENGTH arity_errors arity_error_count)
    if(check_result EQUAL 0 OR NOT arity_error_count EQUAL 4)
        message(FATAL_ERROR
            "terminal input built-ins did not diagnose all extra arguments\n"
            "${check_output}${check_error}")
    endif()
endfunction()

function(run_input_prompt_type_check)
    set(source "${SOURCE_DIR}/tests/native/terminal_input_bad_prompt.zp")
    execute_process(
        COMMAND "${ZP}" check "${source}"
        RESULT_VARIABLE check_result
        OUTPUT_VARIABLE check_output
        ERROR_VARIABLE check_error
        TIMEOUT 15
    )
    string(REGEX MATCHALL "expects a string" prompt_type_errors
        "${check_output}${check_error}")
    list(LENGTH prompt_type_errors prompt_type_error_count)
    if(check_result EQUAL 0 OR NOT prompt_type_error_count EQUAL 4)
        message(FATAL_ERROR
            "terminal input built-ins did not reject all non-string prompts\n"
            "${check_output}${check_error}")
    endif()
endfunction()

set(terminal_input_data
    "Ada Lovelace\npunctuation: !?.,\n\n +42 \n-17\n3.125\n1.25e2\nTrUe\nfAlSe\ntrue\nloop one\nloop two\nUTF-8: Ζ+ 🙂")
set(terminal_input_expected
    "Ada Lovelace\npunctuation: !?.,\nempty line passed\ninteger input passed\ndecimal input passed\nexponent input passed\nmixed-case true passed\nmixed-case false passed\nconditional input passed\nloop one\nloop two\nUTF-8: Ζ+ 🙂\n")
run_input_case(terminal_input "${terminal_input_data}" "${terminal_input_expected}")
run_input_case(terminal_input_line "line with CRLF\r\n" "line with CRLF\n")
run_input_case(terminal_input_line "final line without newline" "final line without newline\n")
run_input_case(terminal_input_eof "" "string EOF is empty\n")
run_input_case(
    terminal_input_prompt
    "Ada Lovelace\n42\n1.75\ntrue\n"
    "Name: Ada Lovelace\nAge: age input passed\nHeight: height input passed\nContinue? boolean input passed\n")
if(HOST_WINDOWS)
    run_input_case(
        terminal_input_prompt_sample
        "17\n"
        "How old are you: Your age is 17\n")
else()
    run_interpreter_input_case(
        terminal_input_prompt_sample
        "17\n"
        "How old are you: Your age is 17\n")
endif()

string(REPEAT "x" 131072 terminal_input_large_data)
run_input_case(
    terminal_input_large
    "${terminal_input_large_data}"
    "${terminal_input_large_data}\n")

string(REPEAT "discarded input line\n" 4999 terminal_input_reassignments)
run_input_case(
    terminal_input_gc_stress
    "held input value\n${terminal_input_reassignments}last input value\n"
    "held input value\nlast input value\n")

run_input_failure(
    terminal_input_invalid_integer
    "12x\n"
    "read_integer: invalid integer input")
run_input_failure(
    terminal_input_invalid_integer
    "+-12\n"
    "read_integer: invalid integer input")
run_input_failure(
    terminal_input_invalid_integer
    "9007199254740992\n"
    "read_integer: value is outside the exactly representable number range")
run_input_failure(
    terminal_input_invalid_integer
    "9223372036854775808\n"
    "read_integer: invalid integer input")
run_input_failure(
    terminal_input_invalid_integer
    ""
    "read_integer: end of input")
run_input_failure(
    terminal_input_invalid_number
    "1.2.3\n"
    "read_number: invalid numeric input")
run_input_failure(
    terminal_input_invalid_number
    "+-1.25\n"
    "read_number: invalid numeric input")
run_input_failure(
    terminal_input_invalid_number
    "1e9999\n"
    "read_number: invalid numeric input")
run_input_failure(
    terminal_input_invalid_number
    ""
    "read_number: end of input")
run_input_failure(
    terminal_input_invalid_boolean
    "yes\n"
    "read_boolean: invalid boolean input")
run_input_failure(
    terminal_input_invalid_boolean
    ""
    "read_boolean: end of input")
run_input_arity_check()
run_input_prompt_type_check()

run_native_interpreter_parity(
    deferred_stack_arguments
    "0\n10\n15\n21\n22\ntwo\nsix\n5\n22\ntwo\nsix\n5\n22\ntwo\nsix\n5\n42.5\ntwo\nsix\nten\ntwelve\n5\n")
run_native_interpreter_parity(
    indexed_compound_assignments
    "11.875\n10\n")
run_native_interpreter_parity(
    register_allocation
    "CHECKSUM register_allocation 318720\n")
run_native_interpreter_parity(
    xmm_direct
    "CHECKSUM xmm_direct 422.5\n")
run_native_interpreter_parity(
    floating_remainder
    "1.5\n-1.5\n1.5\n-1.5\n1\n-1.5\n")
run_native_interpreter_parity(
    systemv_arguments
    "float 0 passed\nfloat 4 passed\nfloat 6 passed\nfloat 7 passed\nfloat 8 passed\nfloat 9 passed\nfloat 12 passed\nfloat 16 passed\ninteger 7 passed\ninteger 9 passed\ninteger 12 passed\ninteger 16 passed\nmixed integer overflow passed\nmixed float overflow passed\npointer-1\npointer-2\npointer-3\npointer-4\npointer-5\npointer-6\npointer-7\npointer-8\npointer-9\ndeferred integer stack passed\ndeferred float stack passed\n")

run_case(empty 0 "")
run_case(main_exit_code 42 "native main returned 42\n")
run_case(integer_return 0 "integer constant passed\n")
run_case(arithmetic 0 "arithmetic passed\n")
execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/native/numeric_parity.zp"
    RESULT_VARIABLE numeric_check_result
    OUTPUT_VARIABLE numeric_check_output
    ERROR_VARIABLE numeric_check_error
    TIMEOUT 10
)
if(NOT numeric_check_result EQUAL 0)
    message(FATAL_ERROR "numeric_parity: zp check failed\n${numeric_check_output}${numeric_check_error}")
endif()
run_case(
    numeric_parity
    0
    "2.75\n3.25\n4.25\n1.75\n1.25\n3.75\n3\n2\n8\n4\n5\n4\n12\n6\n1\n2\n1\neq\nne\nlt\nle\ngt\nge\ninverse\npositive\nnegative\n"
)
run_case(operators 0 "operators passed\n")
run_case(
    bitwise_defined
    0
    "1\n-4\n-9.223372036854776e+18\n-9.223372036854776e+18\n"
)
run_case(member_bitwise_assignment 0 "4\n")
run_case(unicode_characters 0 "é\n🙂\n")
run_case(control_flow 0 "control flow passed\n")
run_case(loop_control 0 "loop control passed\n")
run_case(functions 0 "function calls passed\n")
run_case(switch 0 "switch passed\n")
run_case(output 0 "native backend éЖ🙂 legacy output\ntrue\nfalse\ntrue\n")
run_interpreter_case(output "native backend éЖ🙂 legacy output\ntrue\nfalse\ntrue\n")
run_interpreter_case(
    bitwise_defined
    "1\n-4\n-9.223372036854776e+18\n-9.223372036854776e+18\n"
)
run_interpreter_case(member_bitwise_assignment "4\n")
run_interpreter_case(unicode_characters "é\n🙂\n")
run_case(classes 0 "2\n5\n5\n10\n")
run_interpreter_case(classes "2\n5\n5\n10\n")
run_case(enums 0 "true\n4\ntrue\n")
run_interpreter_case(enums "true\n4\ntrue\n")
run_case(
    string_comparison
    0
    "string equality passed\nstring inequality passed\ndynamic string equality passed\ndynamic string inequality passed\ndynamic conjunction passed\n"
)

run_case(six_arguments 0 "six arguments passed\n")
run_case(default_arguments 0 "default arguments passed\npartial defaults passed\nexplicit arguments passed\nforward default call passed\n")
run_case(short_circuit 0 "and short-circuited\nor short-circuited\nand symbol short-circuited\nor symbol short-circuited\nunexpected side effect\nand evaluated right operand\nunexpected side effect\nor evaluated right operand\n")
run_case(scopes 0 "nested inner\nnested scope retained\ninner assignment\nouter retained\nfunction outer retained\nparameter inner\nparameter retained\nouter assignment\nthen binding\nseparate else binding\nbranch outer retained\nloop binding\nloop shadow\nloop binding\nloop variable retained\nloop outer retained\n")
run_interpreter_case(default_arguments "default arguments passed\npartial defaults passed\nexplicit arguments passed\nforward default call passed\n")
run_interpreter_case(
    numeric_parity
    "2.75\n3.25\n4.25\n1.75\n1.25\n3.75\n3\n2\n8\n4\n5\n4\n12\n6\n1\n2\n1\neq\nne\nlt\nle\ngt\nge\ninverse\npositive\nnegative\n"
)
run_interpreter_case(short_circuit "and short-circuited\nor short-circuited\nand symbol short-circuited\nor symbol short-circuited\nunexpected side effect\nand evaluated right operand\nunexpected side effect\nor evaluated right operand\n")
run_interpreter_case(scopes "nested inner\nnested scope retained\ninner assignment\nouter retained\nfunction outer retained\nparameter inner\nparameter retained\nouter assignment\nthen binding\nseparate else binding\nbranch outer retained\nloop binding\nloop shadow\nloop binding\nloop variable retained\nloop outer retained\n")
run_interpreter_case(
    string_comparison
    "string equality passed\nstring inequality passed\ndynamic string equality passed\ndynamic string inequality passed\ndynamic conjunction passed\n"
)
if(HOST_WINDOWS)
    run_native_build_error(unsupported_say_type "unsupported_say_type.zp:3:8")
endif()
run_native_build_error(
    unknown_dynamic_operation
    "native backend: operations on unknown values are unsupported")
run_interpreter_case(unknown_dynamic_operation "text1\n")
run_interpreter_case(
    arrays
    "empty array passed\ninitialized array read and write passed\narray argument mutation passed\nnested array mutation passed\narrays\nboolean array passed\narray references survived collection\nnative \nunreachable arrays reclaimed\nmanaged array collection passed\n"
)
run_interpreter_case(
    array_length_and_to_string
    "empty=0\nsingle=1\nwords=3\nflags=2\nnested=2\npositive=42\nnegative=-17\nzero=0\ndecimal=2.5\nexpression=7\nfactorial=120\n"
)
run_interpreter_case(
    dynamic_arrays
    "push and aliases passed\ninsert remove and pop passed\ncontains passed\nsmall integer contains passed\nfunction mutation and return passed\ninsert at end passed\nnested array mutation passed\nstring array passed\nglobal array passed\ngeometric growth passed\nheap accounting passed\nclear and reuse passed\nreachable arrays and strings survived collection\nunreachable arrays reclaimed\n"
)
run_check_failure(invalid_default_parameters "parameters without defaults cannot follow")
run_check_failure(invalid_enum_member "enum 'Status' has no member 'Unknown'")
run_check_failure(invalid_class_member "class 'Counter' has no field or method 'missing'")
run_check_failure(length_invalid_argument "length expects an array")
run_check_failure(to_string_invalid_argument "to_string expects a number, got 'string'")
run_check_failure(
    dynamic_array_type_mismatch
    "push value has type 'string', expected 'number'"
)
run_check_failure(
    dynamic_array_invalid_index_type
    "insert index must be a number"
)
run_native_build_error(
    interpreter_only_builtin
    "available only in the interpreter"
)

if(HOST_WINDOWS)
    if(NOT DEFINED GRAPHICS_RUNTIME OR NOT EXISTS "${GRAPHICS_RUNTIME}")
        message(FATAL_ERROR "Windows native graphics runtime was not built")
    endif()

    run_case(
        arrays
        0
        "empty array passed\ninitialized array read and write passed\narray argument mutation passed\nnested array mutation passed\narrays\nboolean array passed\narray references survived collection\nnative \nunreachable arrays reclaimed\nmanaged array collection passed\n"
    )
    run_case(
        array_length_and_to_string
        0
        "empty=0\nsingle=1\nwords=3\nflags=2\nnested=2\npositive=42\nnegative=-17\nzero=0\ndecimal=2.5\nexpression=7\nfactorial=120\n"
    )
    run_case(
        dynamic_arrays
        0
        "push and aliases passed\ninsert remove and pop passed\ncontains passed\nsmall integer contains passed\nfunction mutation and return passed\ninsert at end passed\nnested array mutation passed\nstring array passed\nglobal array passed\ngeometric growth passed\nheap accounting passed\nclear and reuse passed\nreachable arrays and strings survived collection\nunreachable arrays reclaimed\n"
    )
    run_case(
        structs
        0
        "struct type check passed\nstruct reference identity passed\n11\nnative\n19\nnative\n19\nstruct references survived collection\n"
    )
    run_native_runtime_error(
        modulo_by_zero
        "modulo by zero"
    )
    run_native_runtime_error(
        dynamic_array_pop_empty
        "cannot pop from an empty array"
    )
    run_native_runtime_error(
        dynamic_array_insert_invalid
        "array insertion index 2 is out of bounds for array of length 1"
    )
    run_native_runtime_error(
        dynamic_array_remove_invalid
        "array index 1 is out of bounds for array of length 1"
    )
    run_native_runtime_error(
        array_index_out_of_bounds
        "array index 2 is out of bounds for array of length 2"
    )
    run_native_runtime_error(
        array_index_negative
        "array index must be a non-negative integer"
    )
    run_native_runtime_error(
        array_index_store_out_of_bounds
        "array index 2 is out of bounds for array of length 2"
    )
    run_native_build_error(
        array_comparison
        "native backend does not support array comparison"
    )

    run_case(zgraphics_runtime 0 "native ZGraphics passed\n")
    run_case(
        memory_management
        0
        "native managed string allocated\nmanaged!?\nglobal string\nnative managed string reclaimed\nglobal string!\nnative collection completed\n"
    )
    set(memory_invalid_executable "${WORK_DIR}/memory_invalid_threshold.native")
    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/tests/native/memory_invalid_threshold.zp"
                -o "${memory_invalid_executable}"
        RESULT_VARIABLE memory_invalid_build_result
        OUTPUT_VARIABLE memory_invalid_build_stdout
        ERROR_VARIABLE memory_invalid_build_stderr
        TIMEOUT 15
    )
    if(NOT memory_invalid_build_result EQUAL 0)
        message(FATAL_ERROR "invalid memory-threshold test did not build\n${memory_invalid_build_stdout}${memory_invalid_build_stderr}")
    endif()
    execute_process(
        COMMAND "${memory_invalid_executable}"
        RESULT_VARIABLE memory_invalid_run_result
        OUTPUT_VARIABLE memory_invalid_run_stdout
        ERROR_VARIABLE memory_invalid_run_stderr
        TIMEOUT 10
    )
    if(memory_invalid_run_result EQUAL 0 OR
       NOT "${memory_invalid_run_stdout}${memory_invalid_run_stderr}" MATCHES
           "memory threshold must be from 1024 through 1099511627776 bytes")
        message(FATAL_ERROR "native memory runtime did not report an invalid threshold\n${memory_invalid_run_stdout}${memory_invalid_run_stderr}")
    endif()

    set(rendering_executable "${WORK_DIR}/zgraphics_rendering_smoke.native")
    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/tests/graphics/rendering_smoke.zp"
                -o "${rendering_executable}"
        RESULT_VARIABLE rendering_build_result
        OUTPUT_VARIABLE rendering_build_stdout
        ERROR_VARIABLE rendering_build_stderr
        TIMEOUT 15
    )
    if(NOT rendering_build_result EQUAL 0)
        message(FATAL_ERROR "ZGraphics rendering smoke build failed (${rendering_build_result})\n${rendering_build_stdout}${rendering_build_stderr}")
    endif()
    execute_process(
        COMMAND "${rendering_executable}"
        RESULT_VARIABLE rendering_run_result
        OUTPUT_VARIABLE rendering_run_stdout
        ERROR_VARIABLE rendering_run_stderr
        TIMEOUT 10
    )
    if(NOT rendering_run_result EQUAL 0 OR
       NOT "${rendering_run_stdout}" MATCHES "ZGraphics buffered rendering smoke passed")
        message(FATAL_ERROR "ZGraphics native rendering smoke failed (${rendering_run_result})\n${rendering_run_stdout}${rendering_run_stderr}")
    endif()

    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/tests/native/zgraphics_invalid_argument.zp"
                -o "${WORK_DIR}/zgraphics_invalid_argument.native"
        RESULT_VARIABLE invalid_build_result
        OUTPUT_VARIABLE invalid_build_stdout
        ERROR_VARIABLE invalid_build_stderr
        TIMEOUT 15
    )
    if(NOT invalid_build_result EQUAL 0)
        message(FATAL_ERROR "invalid-argument test did not build\n${invalid_build_stdout}${invalid_build_stderr}")
    endif()
    execute_process(
        COMMAND "${WORK_DIR}/zgraphics_invalid_argument.native"
        RESULT_VARIABLE invalid_run_result
        OUTPUT_VARIABLE invalid_run_stdout
        ERROR_VARIABLE invalid_run_stderr
        TIMEOUT 10
    )
    if(invalid_run_result EQUAL 0 OR
       NOT "${invalid_run_stdout}${invalid_run_stderr}" MATCHES "window dimensions must be between 1 and 16384")
        message(FATAL_ERROR "native graphics runtime did not report invalid dimensions\n${invalid_run_stdout}${invalid_run_stderr}")
    endif()

    set(missing_runtime_dir "${WORK_DIR}/missing-runtime")
    file(MAKE_DIRECTORY "${missing_runtime_dir}")
    file(COPY_FILE "${ZP}" "${missing_runtime_dir}/zp.exe")
    file(COPY "${SOURCE_DIR}/stdlib" DESTINATION "${missing_runtime_dir}")
    execute_process(
        COMMAND "${missing_runtime_dir}/zp.exe" build
                "${SOURCE_DIR}/tests/native/zgraphics_runtime.zp"
                -o "${missing_runtime_dir}/not-built.exe"
        RESULT_VARIABLE missing_runtime_result
        OUTPUT_VARIABLE missing_runtime_stdout
        ERROR_VARIABLE missing_runtime_stderr
        TIMEOUT 15
    )
    if(missing_runtime_result EQUAL 0 OR
       NOT "${missing_runtime_stdout}${missing_runtime_stderr}" MATCHES "required native runtime")
        message(FATAL_ERROR "missing graphics runtime was not diagnosed\n${missing_runtime_stdout}${missing_runtime_stderr}")
    endif()
else()
    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/tests/native/dynamic_arrays.zp"
                -o "${WORK_DIR}/dynamic-arrays-unsupported.native"
        RESULT_VARIABLE dynamic_arrays_unsupported_result
        OUTPUT_VARIABLE dynamic_arrays_unsupported_stdout
        ERROR_VARIABLE dynamic_arrays_unsupported_stderr
        TIMEOUT 15
    )
    if(dynamic_arrays_unsupported_result EQUAL 0 OR
       NOT "${dynamic_arrays_unsupported_stdout}${dynamic_arrays_unsupported_stderr}" MATCHES
           "builtin '__zmemory_heapUsed' requires the Windows x64 runtime")
        message(FATAL_ERROR "unsupported native dynamic-array target was not diagnosed\n${dynamic_arrays_unsupported_stdout}${dynamic_arrays_unsupported_stderr}")
    endif()

    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/tests/native/array_index_out_of_bounds.zp"
                -o "${WORK_DIR}/arrays-unsupported.native"
        RESULT_VARIABLE arrays_unsupported_result
        OUTPUT_VARIABLE arrays_unsupported_stdout
        ERROR_VARIABLE arrays_unsupported_stderr
        TIMEOUT 15
    )
    if(arrays_unsupported_result EQUAL 0 OR
       NOT "${arrays_unsupported_stdout}${arrays_unsupported_stderr}" MATCHES
           "managed arrays require the Windows x64 runtime")
        message(FATAL_ERROR "unsupported native array target was not diagnosed\n${arrays_unsupported_stdout}${arrays_unsupported_stderr}")
    endif()

    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/tests/native/unsupported_string_concat.zp"
                -o "${WORK_DIR}/unsupported_string_concat.native"
        RESULT_VARIABLE unsupported_result
        OUTPUT_VARIABLE unsupported_stdout
        ERROR_VARIABLE unsupported_stderr
        TIMEOUT 15
    )
    if(unsupported_result EQUAL 0 OR
       NOT "${unsupported_stdout}${unsupported_stderr}" MATCHES "string concatenation")
        message(FATAL_ERROR "unsupported non-Windows string concatenation was not diagnosed\n${unsupported_stdout}${unsupported_stderr}")
    endif()
endif()
