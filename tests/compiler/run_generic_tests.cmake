if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_check source)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${source}: check failed\n${output}${error}")
    endif()
endfunction()

function(run_success source expected_output)
    execute_process(
        COMMAND "${ZP}" run "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    if(NOT status EQUAL 0 OR NOT output STREQUAL expected_output)
        message(FATAL_ERROR
            "${source}: interpreter mismatch (status ${status})\n"
            "expected: [${expected_output}]\nactual: [${output}]\n${error}")
    endif()
endfunction()

function(expect_check_error source pattern)
    execute_process(
        COMMAND "${ZP}" check "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    if(status EQUAL 0 OR NOT "${output}${error}" MATCHES "${pattern}")
        message(FATAL_ERROR
            "${source}: expected diagnostic matching [${pattern}]\n${output}${error}")
    endif()
endfunction()

function(expect_runtime_error source pattern)
    execute_process(
        COMMAND "${ZP}" run "${SOURCE_DIR}/${source}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    if(status EQUAL 0 OR NOT "${output}${error}" MATCHES "${pattern}")
        message(FATAL_ERROR
            "${source}: expected runtime diagnostic matching [${pattern}]\n${output}${error}")
    endif()
endfunction()

function(run_native source expected_output)
    get_filename_component(source_name "${source}" NAME_WE)
    set(output_path "${WORK_DIR}/${source_name}.exe")
    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/${source}" -o "${output_path}"
        RESULT_VARIABLE build_status
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 40
    )
    if(NOT build_status EQUAL 0)
        message(FATAL_ERROR
            "${source}: native build failed\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${output_path}"
        RESULT_VARIABLE run_status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    file(REMOVE "${output_path}")
    if(NOT run_status EQUAL 0 OR NOT output STREQUAL expected_output)
        message(FATAL_ERROR
            "${source}: native mismatch (status ${run_status})\n"
            "expected: [${expected_output}]\nactual: [${output}]\n${error}")
    endif()
endfunction()

function(expect_native_runtime_error source pattern)
    get_filename_component(source_name "${source}" NAME_WE)
    set(output_path "${WORK_DIR}/${source_name}.exe")
    execute_process(
        COMMAND "${ZP}" build "${SOURCE_DIR}/${source}" -o "${output_path}"
        RESULT_VARIABLE build_status
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 40
    )
    if(NOT build_status EQUAL 0)
        message(FATAL_ERROR
            "${source}: native build failed\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${output_path}"
        RESULT_VARIABLE run_status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    file(REMOVE "${output_path}")
    if(run_status EQUAL 0 OR NOT "${output}${error}" MATCHES "${pattern}")
        message(FATAL_ERROR
            "${source}: expected native runtime diagnostic matching [${pattern}]\n${output}${error}")
    endif()
endfunction()

set(generic_output "ready\ntrue\ndefault\n21\n14\n42\n42\n51\n62\nclass\n9\n")
set(collection_output "2\n8\nlast\n4\n1\ntrue\ntrue\ntrue\n42\n7\n42\n0\n")

run_check("tests/compiler/generics_smoke.zp")
run_check("tests/compiler/generic_collections.zp")
run_success("tests/compiler/generics_smoke.zp" "${generic_output}")
run_success("tests/compiler/generic_collections.zp" "${collection_output}")
run_success("tests/compiler/explicit_generic_empty_array.zp" "0\n")
expect_check_error(
    "tests/compiler/invalid_generic_call.zp"
    "conflicts with parameter type|does not satisfy constraints|cannot infer generic type parameter")
expect_check_error(
    "tests/compiler/invalid_generic_call.zp"
    "constructor argument for field 1 of 'Box'.*expected 'number'")
expect_check_error(
    "tests/compiler/unknown_parameter_boundary.zp"
    "argument 1 has type 'unknown', expected 'number'")
expect_check_error(
    "tests/compiler/unknown_return_boundary.zp"
    "returns 'number' but this return provides 'unknown'")
expect_check_error(
    "tests/compiler/unknown_assignment_boundary.zp"
    "cannot assign 'unknown' to 'number'")
expect_check_error(
    "tests/compiler/unknown_generic_call.zp"
    "generic argument 1 has type 'unknown'")
expect_check_error(
    "tests/compiler/recursive_generic_specialization.zp"
    "recursive generic specialization.*expands its type arguments")
expect_runtime_error(
    "tests/compiler/invalid_generic_map.zp"
    "Map keys and values must have matching lengths")
expect_runtime_error(
    "tests/compiler/invalid_generic_optional.zp"
    "Optional can contain at most one value")

if(HOST_WINDOWS)
    run_native("tests/compiler/generics_smoke.zp" "${generic_output}")
    run_native("tests/compiler/generic_collections.zp" "${collection_output}")
    expect_native_runtime_error(
        "tests/compiler/invalid_generic_map.zp"
        "Map keys and values must have matching lengths")
    expect_native_runtime_error(
        "tests/compiler/invalid_generic_optional.zp"
        "Optional can contain at most one value")
endif()
