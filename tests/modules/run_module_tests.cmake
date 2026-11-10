if(NOT DEFINED ZP OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(write_source relative_path contents)
    get_filename_component(directory "${WORK_DIR}/${relative_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${WORK_DIR}/${relative_path}" "${contents}")
endfunction()

function(check_program name expected_status expected_pattern)
    set(directory "${WORK_DIR}/${name}")
    execute_process(
        COMMAND "${ZP}" check main.zp --diagnostics=json
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    set(combined "${output}\n${error}")
    if(expected_status STREQUAL "success")
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${name}: expected success, got ${status}\n${combined}")
        endif()
    else()
        if(status EQUAL 0)
            message(FATAL_ERROR "${name}: expected failure\n${combined}")
        endif()
        if(NOT combined MATCHES "${expected_pattern}")
            message(FATAL_ERROR "${name}: diagnostic did not match '${expected_pattern}'\n${combined}")
        endif()
    endif()
endfunction()

function(run_program name expected_output)
    set(directory "${WORK_DIR}/${name}")
    execute_process(
        COMMAND "${ZP}" run main.zp
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0 OR NOT output STREQUAL expected_output)
        message(FATAL_ERROR "${name}: interpreter mismatch (status ${status})\nexpected: [${expected_output}]\nactual: [${output}]\n${error}")
    endif()
endfunction()

set(simple_dir "${WORK_DIR}/single module")
file(MAKE_DIRECTORY "${simple_dir}")
file(WRITE "${simple_dir}/header.zh" "pub fn add(a: number, b: number) -> number:\n    return a + b\nend\n")
file(WRITE "${simple_dir}/main.zp" "use header.zh\nstart:\n    say(add(10, 20))\nstop\n")
check_program("single module" success "")
run_program("single module" "30\n")

set(multiple_dir "${WORK_DIR}/multiple")
file(MAKE_DIRECTORY "${multiple_dir}/sub")
file(WRITE "${multiple_dir}/base.zh" "pub const baseValue: number = 40\npub fn base() -> number:\n    return baseValue\nend\n")
file(WRITE "${multiple_dir}/sub/math.zh" "use ../base.zh\npub const incrementValue: number = 2\npub fn answer() -> number:\n    return base() + incrementValue\nend\n")
file(WRITE "${multiple_dir}/main.zp" "use base.zh\nuse sub/math.zh\nstart:\n    say(answer())\nstop\n")
check_program("multiple" success "")
run_program("multiple" "42\n")

set(quoted_dir "${WORK_DIR}/quoted and spaces")
file(MAKE_DIRECTORY "${quoted_dir}/modules")
file(WRITE "${quoted_dir}/modules/network.zh" "pub fn networkCode() -> number:\n    return 7\nend\n")
file(WRITE "${quoted_dir}/main.zp" "use \"modules/network.zh\"\nstart:\n    say(networkCode())\nstop\n")
check_program("quoted and spaces" success "")
run_program("quoted and spaces" "7\n")

set(duplicate_dir "${WORK_DIR}/duplicate use")
file(MAKE_DIRECTORY "${duplicate_dir}")
file(WRITE "${duplicate_dir}/once.zh" "pub fn once() -> number:\n    return 3\nend\n")
file(WRITE "${duplicate_dir}/main.zp" "use once.zh\nuse \"once.zh\"\nstart:\n    say(once())\nstop\n")
check_program("duplicate use" success "")
run_program("duplicate use" "3\n")

set(missing_dir "${WORK_DIR}/missing")
file(MAKE_DIRECTORY "${missing_dir}")
file(WRITE "${missing_dir}/main.zp" "use missing.zh\nstart:\nstop\n")
check_program("missing" failure "module.*missing\\.zh.*not found")

set(cycle_dir "${WORK_DIR}/cycle")
file(MAKE_DIRECTORY "${cycle_dir}")
file(WRITE "${cycle_dir}/main.zp" "use first.zh\nstart:\nstop\n")
file(WRITE "${cycle_dir}/first.zh" "use second.zh\nfn first():\nend\n")
file(WRITE "${cycle_dir}/second.zh" "use first.zh\nfn second():\nend\n")
check_program("cycle" failure "circular module dependency")

set(duplicate_function_dir "${WORK_DIR}/duplicate functions")
file(MAKE_DIRECTORY "${duplicate_function_dir}")
file(WRITE "${duplicate_function_dir}/a.zh" "pub fn same() -> number:\n    return 1\nend\n")
file(WRITE "${duplicate_function_dir}/b.zh" "pub fn same() -> number:\n    return 2\nend\n")
file(WRITE "${duplicate_function_dir}/main.zp"
    "use a.zh\nuse b.zh\nstart:\n    say(a.same())\n    say(b.same())\nstop\n")
check_program("duplicate functions" success "")
run_program("duplicate functions" "1\n2\n")

set(undefined_dir "${WORK_DIR}/undefined in module")
file(MAKE_DIRECTORY "${undefined_dir}")
file(WRITE "${undefined_dir}/bad.zh" "fn bad():\n    say(notDefined)\nend\n")
file(WRITE "${undefined_dir}/main.zp" "use bad.zh\nstart:\nstop\n")
check_program("undefined in module" failure "undefined identifier 'notDefined'")

set(semantic_dir "${WORK_DIR}/semantic in module")
file(MAKE_DIRECTORY "${semantic_dir}")
file(WRITE "${semantic_dir}/bad.zh" "fn bad() -> number:\n    return \"not a number\"\nend\n")
file(WRITE "${semantic_dir}/main.zp" "use bad.zh\nstart:\nstop\n")
check_program("semantic in module" failure "function 'bad' returns")

set(invalid_dir "${WORK_DIR}/invalid syntax")
file(MAKE_DIRECTORY "${invalid_dir}")
file(WRITE "${invalid_dir}/bad.zh" "fn broken(:\nend\n")
file(WRITE "${invalid_dir}/main.zp" "use bad.zh\nstart:\nstop\n")
check_program("invalid syntax" failure "\"file\":\".*bad\\.zh\"")

set(start_dir "${WORK_DIR}/start in module")
file(MAKE_DIRECTORY "${start_dir}")
file(WRITE "${start_dir}/bad.zh" "start:\n    say(\"no\")\nstop\n")
file(WRITE "${start_dir}/main.zp" "use bad.zh\nstart:\nstop\n")
check_program("start in module" failure "cannot contain a start block")

set(nested_start_dir "${WORK_DIR}/nested start in module")
file(MAKE_DIRECTORY "${nested_start_dir}")
file(WRITE "${nested_start_dir}/bad.zh" "fn nested():\n    start:\n    stop\nend\n")
file(WRITE "${nested_start_dir}/main.zp" "use bad.zh\nstart:\nstop\n")
check_program("nested start in module" failure "cannot contain a start block")

set(module_entry_dir "${WORK_DIR}/module entry")
file(MAKE_DIRECTORY "${module_entry_dir}")
file(WRITE "${module_entry_dir}/direct.zh" "fn direct():\nend\n")
execute_process(
    COMMAND "${ZP}" check direct.zh --diagnostics=json
    WORKING_DIRECTORY "${module_entry_dir}"
    RESULT_VARIABLE module_entry_status
    OUTPUT_VARIABLE module_entry_output
    ERROR_VARIABLE module_entry_error
)
if(module_entry_status EQUAL 0 OR NOT "${module_entry_error}" MATCHES "entry source file must use the \\.zp extension")
    message(FATAL_ERROR "a .zh file must not be accepted as a compilation entry:\n${module_entry_output}\n${module_entry_error}")
endif()

set(global_dir "${WORK_DIR}/module constants")
file(MAKE_DIRECTORY "${global_dir}")
file(WRITE "${global_dir}/constants.zh" "pub const answer: number = 42\npub let counter: number = 0\npub fn increment() -> number:\n    counter += 1\n    return answer + counter\nend\n")
file(WRITE "${global_dir}/main.zp" "use constants.zh\nfn main():\n    say(increment())\n    say(increment())\nend\n")
file(WRITE "${global_dir}/native.zp" "use constants.zh\nfn main() -> number:\n    increment()\n    return increment() - 44\nend\n")
check_program("module constants" success "")
run_program("module constants" "43\n44\n")

execute_process(
    COMMAND "${ZP}" build native.zp -o module-global.exe
    WORKING_DIRECTORY "${global_dir}"
    RESULT_VARIABLE native_global_status
    OUTPUT_VARIABLE native_global_output
    ERROR_VARIABLE native_global_error
)
if(NOT native_global_status EQUAL 0)
    message(FATAL_ERROR "native module-global build failed:\n${native_global_output}\n${native_global_error}")
endif()
execute_process(
    COMMAND "${global_dir}/module-global.exe"
    WORKING_DIRECTORY "${global_dir}"
    RESULT_VARIABLE native_global_run_status
    OUTPUT_VARIABLE native_global_run_output
    ERROR_VARIABLE native_global_run_error
)
if(NOT native_global_run_status EQUAL 0)
    message(FATAL_ERROR "native module-global execution failed: ${native_global_run_status}\n${native_global_run_output}\n${native_global_run_error}")
endif()

set(backslash_dir "${WORK_DIR}/backslash path")
file(MAKE_DIRECTORY "${backslash_dir}/nested")
file(WRITE "${backslash_dir}/nested/path.zh" "pub fn pathValue() -> number:\n    return 9\nend\n")
file(WRITE "${backslash_dir}/main.zp" "use nested\\path.zh\nstart:\n    say(pathValue())\nstop\n")
check_program("backslash path" success "")
run_program("backslash path" "9\n")

set(absolute_dir "${WORK_DIR}/absolute path policy")
file(MAKE_DIRECTORY "${absolute_dir}")
file(WRITE "${absolute_dir}/main.zp" "use \"C:/not-permitted.zh\"\nstart:\nstop\n")
check_program("absolute path policy" failure "absolute module paths are not permitted")

set(cwd_dir "${WORK_DIR}/cwd independent")
file(MAKE_DIRECTORY "${cwd_dir}/modules" "${cwd_dir}/other-working-directory")
file(WRITE "${cwd_dir}/modules/relative.zh" "pub fn relativeValue() -> number:\n    return 5\nend\n")
file(WRITE "${cwd_dir}/main.zp" "use modules/relative.zh\nstart:\n    say(relativeValue())\nstop\n")
execute_process(
    COMMAND "${ZP}" run "${cwd_dir}/main.zp"
    WORKING_DIRECTORY "${cwd_dir}/other-working-directory"
    RESULT_VARIABLE cwd_status
    OUTPUT_VARIABLE cwd_output
    ERROR_VARIABLE cwd_error
)
if(NOT cwd_status EQUAL 0 OR NOT cwd_output STREQUAL "5\n")
    message(FATAL_ERROR "cwd independent resolution failed: ${cwd_status}\n${cwd_output}\n${cwd_error}")
endif()

set(native_dir "${WORK_DIR}/native imported function")
file(MAKE_DIRECTORY "${native_dir}")
file(WRITE "${native_dir}/native.zh" "pub fn imported() -> number:\n    return 23\nend\n")
file(WRITE "${native_dir}/main.zp" "use native.zh\nfn main() -> number:\n    return imported() - 23\nend\n")
execute_process(
    COMMAND "${ZP}" build main.zp -o module-native.exe
    WORKING_DIRECTORY "${native_dir}"
    RESULT_VARIABLE native_build_status
    OUTPUT_VARIABLE native_build_output
    ERROR_VARIABLE native_build_error
)
if(NOT native_build_status EQUAL 0)
    message(FATAL_ERROR "native module build failed:\n${native_build_output}\n${native_build_error}")
endif()
if(WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    execute_process(
        COMMAND "${native_dir}/module-native.exe"
        WORKING_DIRECTORY "${native_dir}"
        RESULT_VARIABLE native_run_status
        OUTPUT_VARIABLE native_run_output
        ERROR_VARIABLE native_run_error
    )
    if(NOT native_run_status EQUAL 0)
        message(FATAL_ERROR "native module execution failed: ${native_run_status}\n${native_run_output}\n${native_run_error}")
    endif()
endif()

set(qualified_dir "${WORK_DIR}/qualified names")
file(MAKE_DIRECTORY "${qualified_dir}")
file(WRITE "${qualified_dir}/math.zh"
    "pub const answer: number = 42\npub let counter: number = 0\npub fn add(a: number, b: number) -> number:\n    return a + b\nend\nfn privateValue() -> number:\n    return 99\nend\n")
file(WRITE "${qualified_dir}/main.zp"
    "use math.zh\nstart:\n    math.counter += 1\n    say(math.add(math.answer, math.counter))\nstop\n")
check_program("qualified names" success "")
run_program("qualified names" "43\n")

set(qualified_native_dir "${WORK_DIR}/native module globals")
file(MAKE_DIRECTORY "${qualified_native_dir}")
file(WRITE "${qualified_native_dir}/state.zh"
    "pub const base: number = 40\npub let counter: number = 1\npub fn next() -> number:\n    counter += 1\n    return base + counter\nend\n")
file(WRITE "${qualified_native_dir}/main.zp"
    "use state.zh\nfn main() -> number:\n    state.next()\n    return state.next() - 43\nend\n")
execute_process(
    COMMAND "${ZP}" build main.zp -o module-globals.exe
    WORKING_DIRECTORY "${qualified_native_dir}"
    RESULT_VARIABLE native_globals_status
    OUTPUT_VARIABLE native_globals_output
    ERROR_VARIABLE native_globals_error
)
if(NOT native_globals_status EQUAL 0)
    message(FATAL_ERROR "native module-global build failed:\n${native_globals_output}\n${native_globals_error}")
endif()
execute_process(
    COMMAND "${qualified_native_dir}/module-globals.exe"
    WORKING_DIRECTORY "${qualified_native_dir}"
    RESULT_VARIABLE native_globals_run_status
    OUTPUT_VARIABLE native_globals_run_output
    ERROR_VARIABLE native_globals_run_error
)
if(NOT native_globals_run_status EQUAL 0)
    message(FATAL_ERROR "native module-global execution mismatch:\n${native_globals_run_output}\n${native_globals_run_error}")
endif()

set(private_dir "${WORK_DIR}/private access")
file(MAKE_DIRECTORY "${private_dir}")
file(WRITE "${private_dir}/math.zh"
    "fn hidden() -> number:\n    return 2\nend\npub fn visible() -> number:\n    return hidden()\nend\n")
file(WRITE "${private_dir}/main.zp"
    "use math.zh\nstart:\n    say(math.hidden())\nstop\n")
check_program("private access" failure "private to module")

set(private_unqualified_dir "${WORK_DIR}/private unqualified access")
file(MAKE_DIRECTORY "${private_unqualified_dir}")
file(WRITE "${private_unqualified_dir}/math.zh" "fn hidden() -> number:\n    return 2\nend\n")
file(WRITE "${private_unqualified_dir}/main.zp"
    "use math.zh\nstart:\n    say(hidden())\nstop\n")
check_program("private unqualified access" failure "private to module")

set(private_type_dir "${WORK_DIR}/private type access")
file(MAKE_DIRECTORY "${private_type_dir}")
file(WRITE "${private_type_dir}/types.zh" "struct Secret:\n    value: number\nend\n")
file(WRITE "${private_type_dir}/main.zp"
    "use types.zh\nstart:\n    let item: Secret\nstop\n")
check_program("private type access" failure "type 'Secret' is private to module")

set(private_variable_dir "${WORK_DIR}/private variable access")
file(MAKE_DIRECTORY "${private_variable_dir}")
file(WRITE "${private_variable_dir}/values.zh" "let hiddenValue: number = 5\n")
file(WRITE "${private_variable_dir}/main.zp"
    "use values.zh\nstart:\n    say(hiddenValue)\nstop\n")
check_program("private variable access" failure "private to module")

set(public_type_dir "${WORK_DIR}/public type access")
file(MAKE_DIRECTORY "${public_type_dir}")
file(WRITE "${public_type_dir}/types.zh" "pub struct Record:\n    value: number\nend\n")
file(WRITE "${public_type_dir}/hiddenType.zh" "struct Record:\n    hidden: number\nend\n")
file(WRITE "${public_type_dir}/main.zp"
    "use types.zh\nuse hiddenType.zh\nstart:\n    let item: Record\nstop\n")
check_program("public type access" success "")

set(public_struct_runtime_dir "${WORK_DIR}/public struct runtime")
file(MAKE_DIRECTORY "${public_struct_runtime_dir}")
file(WRITE "${public_struct_runtime_dir}/types.zh"
    "pub struct Record:\n    value: number\nend\n")
file(WRITE "${public_struct_runtime_dir}/main.zp"
    "use types.zh\nstart:\n    let item = Record(42)\n    say(item.value)\nstop\n")
check_program("public struct runtime" success "")
run_program("public struct runtime" "42\n")

set(duplicate_type_dir "${WORK_DIR}/duplicate type declaration")
file(MAKE_DIRECTORY "${duplicate_type_dir}")
file(WRITE "${duplicate_type_dir}/types.zh"
    "pub struct Record:\n    first: number\nend\npub struct Record:\n    second: number\nend\n")
file(WRITE "${duplicate_type_dir}/main.zp" "use types.zh\nstart:\nstop\n")
check_program("duplicate type declaration" failure "duplicate type declaration")

set(ambiguous_type_dir "${WORK_DIR}/ambiguous public type")
file(MAKE_DIRECTORY "${ambiguous_type_dir}")
file(WRITE "${ambiguous_type_dir}/left.zh" "pub struct Shared:\n    left: number\nend\n")
file(WRITE "${ambiguous_type_dir}/right.zh" "pub struct Shared:\n    right: number\nend\n")
file(WRITE "${ambiguous_type_dir}/main.zp"
    "use left.zh as leftModule\nuse right.zh as rightModule\nstart:\n    let item: Shared\nstop\n")
check_program("ambiguous public type" failure "ambiguous across imported modules")

set(alias_dir "${WORK_DIR}/duplicate alias")
file(MAKE_DIRECTORY "${alias_dir}")
file(WRITE "${alias_dir}/one.zh" "pub fn one():\nend\n")
file(WRITE "${alias_dir}/two.zh" "pub fn two():\nend\n")
file(WRITE "${alias_dir}/main.zp"
    "use one.zh as common\nuse two.zh as common\nstart:\nstop\n")
check_program("duplicate alias" failure "ambiguous module name")

set(duplicate_name_dir "${WORK_DIR}/duplicate default module name")
file(MAKE_DIRECTORY "${duplicate_name_dir}/one" "${duplicate_name_dir}/two")
file(WRITE "${duplicate_name_dir}/one/shared.zh" "pub fn one():\nend\n")
file(WRITE "${duplicate_name_dir}/two/shared.zh" "pub fn two():\nend\n")
file(WRITE "${duplicate_name_dir}/main.zp"
    "use one/shared.zh\nuse two/shared.zh\nstart:\nstop\n")
check_program("duplicate default module name" failure "ambiguous module name")

set(alias_syntax_dir "${WORK_DIR}/module alias")
file(MAKE_DIRECTORY "${alias_syntax_dir}")
file(WRITE "${alias_syntax_dir}/arithmetic.zh" "pub fn twice(value: number) -> number:\n    return value * 2\nend\n")
file(WRITE "${alias_syntax_dir}/main.zp"
    "use arithmetic.zh as calc\nstart:\n    say(calc.twice(21))\nstop\n")
check_program("module alias" success "")
run_program("module alias" "42\n")

set(stdlib_dir "${WORK_DIR}/standard library")
file(MAKE_DIRECTORY "${stdlib_dir}/stdlib/math")
file(COPY "${SOURCE_DIR}/stdlib/math/math.zh" DESTINATION "${stdlib_dir}/stdlib/math")
file(WRITE "${stdlib_dir}/main.zp"
    "use \"stdlib/math/math.zh\" as math\nstart:\n    say(math.abs(-12))\n    say(math.min(2, 9))\n    say(math.max(2, 9))\n    say(math.clamp(20, 0, 10))\nstop\n")
check_program("standard library" success "")
run_program("standard library" "12\n2\n9\n10\n")
file(WRITE "${stdlib_dir}/native.zp"
    "use \"stdlib/math/math.zh\" as math\nfn main() -> number:\n    return math.abs(-12) + math.min(2, 9) + math.max(2, 9) + math.clamp(20, 0, 10) - 33\nend\n")
execute_process(
    COMMAND "${ZP}" build native.zp -o stdlib-native.exe
    WORKING_DIRECTORY "${stdlib_dir}"
    RESULT_VARIABLE stdlib_native_status
    OUTPUT_VARIABLE stdlib_native_output
    ERROR_VARIABLE stdlib_native_error
)
if(NOT stdlib_native_status EQUAL 0)
    message(FATAL_ERROR "native standard-library build failed:\n${stdlib_native_output}\n${stdlib_native_error}")
endif()
execute_process(
    COMMAND "${stdlib_dir}/stdlib-native.exe"
    WORKING_DIRECTORY "${stdlib_dir}"
    RESULT_VARIABLE stdlib_native_run_status
    OUTPUT_VARIABLE stdlib_native_run_output
    ERROR_VARIABLE stdlib_native_run_error
)
if(NOT stdlib_native_run_status EQUAL 0)
    message(FATAL_ERROR "native standard-library execution failed: ${stdlib_native_run_status}\n${stdlib_native_run_output}\n${stdlib_native_run_error}")
endif()

message(STATUS "Z+ module checks passed")
