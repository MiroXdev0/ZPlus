if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

function(run_success source expected_output)
    execute_process(
        COMMAND "${ZP}" run "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 10
    )
    if(NOT status EQUAL 0 OR NOT output STREQUAL expected_output)
        message(FATAL_ERROR "${source}: interpreter mismatch (status ${status})\nexpected: [${expected_output}]\nactual: [${output}]\n${error}")
    endif()
endfunction()

function(expect_runtime_error source pattern)
    execute_process(
        COMMAND "${ZP}" run "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 10
    )
    set(combined "${output}\n${error}")
    if(status EQUAL 0 OR NOT combined MATCHES "${pattern}")
        message(FATAL_ERROR "${source}: expected runtime diagnostic matching [${pattern}]\n${combined}")
    endif()
endfunction()

function(expect_check_error source pattern)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 10
    )
    set(combined "${output}\n${error}")
    if(status EQUAL 0 OR NOT combined MATCHES "${pattern}")
        message(FATAL_ERROR "${source}: expected check diagnostic matching [${pattern}]\n${combined}")
    endif()
endfunction()

function(expect_native_build_error source pattern)
    get_filename_component(source_name "${source}" NAME_WE)
    set(output_path "${WORK_DIR}/unsupported-${source_name}.exe")
    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/${source}" -o "${output_path}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    file(REMOVE "${output_path}")
    if(status EQUAL 0 OR NOT "${output}${error}" MATCHES "${pattern}")
        message(FATAL_ERROR "${source}: expected native rejection matching [${pattern}]\n${output}${error}")
    endif()
endfunction()

run_success(
    "tests/runtime/arrays.zp"
    "0\n10\n100\n7\n3\nbeta\ntrue\nb\n5\n5\nz+\n2\n"
)
run_success(
    "tests/runtime/structs.zp"
    "true\nfalse\nis player\nAlex\n100\n0\n90\n50\n4\nAlex\n95\n95\n"
)
run_success(
    "tests/runtime/struct_gc.zp"
    "kept\n1499\n"
)
run_success(
    "tests/runtime/output.zp"
    "value=42true[1, 2] éЖ🙂value=42true[1, 2] éЖ🙂\n"
)

execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/runtime/output.zp" --diagnostics=json
    RESULT_VARIABLE output_check_status
    OUTPUT_VARIABLE output_check_stdout
    ERROR_VARIABLE output_check_stderr
    TIMEOUT 10
)
if(NOT output_check_status EQUAL 0 OR
   NOT output_check_stderr MATCHES "\"severity\":\"warning\".*printm\\(\\) is deprecated; use printz\\(\\) instead")
    message(FATAL_ERROR "printm compatibility call did not report its deprecation warning\n${output_check_stdout}${output_check_stderr}")
endif()

execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/native/output.zp" --diagnostics=json
    RESULT_VARIABLE native_output_check_status
    OUTPUT_VARIABLE native_output_check_stdout
    ERROR_VARIABLE native_output_check_stderr
    TIMEOUT 10
)
if(NOT native_output_check_status EQUAL 0 OR
   NOT native_output_check_stderr MATCHES "\"severity\":\"warning\".*printm\\(\\) is deprecated; use printz\\(\\) instead")
    message(FATAL_ERROR "native printm compatibility call did not report its deprecation warning\n${native_output_check_stdout}${native_output_check_stderr}")
endif()

execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/runtime/printz_only.zp" --diagnostics=json
    RESULT_VARIABLE printz_check_status
    OUTPUT_VARIABLE printz_check_stdout
    ERROR_VARIABLE printz_check_stderr
    TIMEOUT 10
)
if(NOT printz_check_status EQUAL 0 OR printz_check_stderr MATCHES "\"severity\":\"warning\"")
    message(FATAL_ERROR "printz unexpectedly emitted a diagnostic warning\n${printz_check_stdout}${printz_check_stderr}")
endif()

execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/runtime/array_assignment_type_mismatch.zp" --diagnostics=json
    RESULT_VARIABLE type_diagnostic_status
    OUTPUT_VARIABLE type_diagnostic_stdout
    ERROR_VARIABLE type_diagnostic_stderr
    TIMEOUT 10
)
if(type_diagnostic_status EQUAL 0 OR
   NOT type_diagnostic_stderr MATCHES "\"category\":\"Type error\"" OR
   NOT type_diagnostic_stderr MATCHES "\"sourceLine\":\".*values = \\[.*\"" OR
   NOT type_diagnostic_stderr MATCHES "\"suggestion\":\"\"")
    message(FATAL_ERROR "type diagnostic is missing its category or source excerpt\n${type_diagnostic_stdout}${type_diagnostic_stderr}")
endif()

execute_process(
    COMMAND "${ZP}" check "${SOURCE_DIR}/tests/runtime/array_assignment_type_mismatch.zp"
    RESULT_VARIABLE type_text_status
    OUTPUT_VARIABLE type_text_stdout
    ERROR_VARIABLE type_text_stderr
    TIMEOUT 10
)
if(type_text_status EQUAL 0 OR
   NOT type_text_stderr MATCHES "Type error" OR
   NOT type_text_stderr MATCHES "\\^")
    message(FATAL_ERROR "text diagnostic is missing its category or caret\n${type_text_stdout}${type_text_stderr}")
endif()

run_success("tests/runtime/printz_only.zp" "official")
run_success(
    "tests/native/dynamic_arrays.zp"
    "push and aliases passed\ninsert remove and pop passed\ncontains passed\nsmall integer contains passed\nfunction mutation and return passed\ninsert at end passed\nnested array mutation passed\nstring array passed\nglobal array passed\ngeometric growth passed\nheap accounting passed\nclear and reuse passed\nreachable arrays and strings survived collection\nunreachable arrays reclaimed\n"
)
run_success(
    "tests/runtime/bindings.zp"
    "4\n6\n7\n6\n120\n2\n3\n"
)
run_success(
    "tests/runtime/function_calls.zp"
    "9\n5\n45\n7\n9\n4\n120\n13\n13\n6\n"
)
run_success(
    "tests/runtime/memory_management.zp"
    "0\n30\nZ+ memory\ntrue\ntrue\n30\n42\n4.49985e+08\ntrue\ntrue\ntrue\n2\n"
)
run_success(
    "tests/native/scopes.zp"
    "nested inner\nnested scope retained\ninner assignment\nouter retained\nfunction outer retained\nparameter inner\nparameter retained\nouter assignment\nthen binding\nseparate else binding\nbranch outer retained\nloop binding\nloop shadow\nloop binding\nloop variable retained\nloop outer retained\n"
)
run_success(
    "tests/runtime/array_scopes.zp"
    "4\n6\n7\n9\n2\n1\n2\n9\n"
)

expect_check_error(
    "tests/runtime/array_type_mismatch.zp"
    "array elements must have compatible types"
)
expect_check_error(
    "tests/runtime/invalid_string_index.zp"
    "value is not indexable"
)
expect_check_error(
    "tests/runtime/array_invalid_index_type.zp"
    "array index must be a number"
)
expect_check_error(
    "tests/runtime/array_assignment_type_mismatch.zp"
    "cannot assign"
)
expect_check_error(
    "tests/runtime/struct_missing_required.zp"
    "constructor for 'Pair' expects 2 arguments"
)
expect_check_error(
    "tests/runtime/struct_unknown_field.zp"
    "has no field 'mana'"
)
expect_check_error(
    "tests/runtime/struct_field_type_mismatch.zp"
    "cannot assign 'string' to 'number'"
)
expect_check_error(
    "tests/runtime/struct_unknown_type_check.zp"
    "unknown type 'Missing'"
)
expect_check_error(
    "tests/runtime/array_argument_type_mismatch.zp"
    "expected 'number\\[\\]'"
)
expect_check_error(
    "tests/runtime/array_return_type_mismatch.zp"
    "returns 'number"
)
expect_check_error(
    "tests/native/dynamic_array_type_mismatch.zp"
    "push value has type 'string', expected 'number'"
)
expect_check_error(
    "tests/native/dynamic_array_invalid_index_type.zp"
    "insert index must be a number"
)
expect_check_error(
    "tests/runtime/scoped_binding_leak.zp"
    "undefined identifier 'hidden'"
)

expect_runtime_error(
    "tests/runtime/array_index_out_of_bounds.zp"
    "tests[/\\\\]runtime[/\\\\]array_index_out_of_bounds\\.zp:[0-9]+:[0-9]+: array index 4\\.000000 is out of bounds.*at readPastEnd.*at main"
)
expect_runtime_error(
    "tests/native/dynamic_array_pop_empty.zp"
    "cannot pop from an empty array"
)
expect_runtime_error(
    "tests/native/dynamic_array_insert_invalid.zp"
    "array insertion index 2 is out of bounds for array of length 1"
)
expect_runtime_error(
    "tests/native/dynamic_array_remove_invalid.zp"
    "array index 1 is out of bounds for array of length 1"
)
run_success(
    "tests/runtime/division_by_zero.zp"
    "inf\n"
)
expect_runtime_error(
    "tests/runtime/modules/main.zp"
    "tests[/\\\\]runtime[/\\\\]modules[/\\\\]bad_module\\.zh:[0-9]+:[0-9]+: array index 2\\.000000 is out of bounds.*at bad::readPastEnd.*at main"
)
expect_runtime_error(
    "tests/runtime/memory_invalid_threshold.zp"
    "management\\.zh:[0-9]+:[0-9]+: memory threshold must be an integer"
)
