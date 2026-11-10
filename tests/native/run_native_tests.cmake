if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_case name expected_exit expected_output)
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
       NOT "${build_stdout}${build_stderr}" MATCHES "${expected_message}")
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
run_case(control_flow 0 "control flow passed\n")
run_case(loop_control 0 "loop control passed\n")
run_case(functions 0 "function calls passed\n")
run_case(switch 0 "switch passed\n")
run_case(output 0 "native backend éЖ🙂 legacy output\n")
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
run_interpreter_case(
    arrays
    "empty array passed\ninitialized array read and write passed\narray argument mutation passed\nnested array mutation passed\narrays\nboolean array passed\narray references survived collection\nnative \nunreachable arrays reclaimed\nmanaged array collection passed\n"
)
run_interpreter_case(
    dynamic_arrays
    "push and aliases passed\ninsert remove and pop passed\ncontains passed\nsmall integer contains passed\nfunction mutation and return passed\ninsert at end passed\nnested array mutation passed\nstring array passed\nglobal array passed\ngeometric growth passed\nheap accounting passed\nclear and reuse passed\nreachable arrays and strings survived collection\nunreachable arrays reclaimed\n"
)
run_check_failure(invalid_default_parameters "parameters without defaults cannot follow")
run_check_failure(
    dynamic_array_type_mismatch
    "push value has type 'string', expected 'number'"
)
run_check_failure(
    dynamic_array_invalid_index_type
    "insert index must be a number"
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
           "dynamic array operations require the Windows x64 managed runtime")
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
