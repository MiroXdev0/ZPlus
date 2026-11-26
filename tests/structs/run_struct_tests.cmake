if(NOT DEFINED ZP OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(write_module path contents)
    get_filename_component(directory "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${path}" "start:\n${contents}\nstop\n")
endfunction()

function(check_success name)
    execute_process(
        COMMAND "${ZP}" check main.zp --diagnostics=json
        WORKING_DIRECTORY "${WORK_DIR}/${name}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: expected successful check (${status})\n${output}${error}")
    endif()
endfunction()

function(run_parity name expected)
    set(directory "${WORK_DIR}/${name}")
    execute_process(
        COMMAND "${ZP}" run main.zp
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE interpreter_status
        OUTPUT_VARIABLE interpreter_output
        ERROR_VARIABLE interpreter_error
        TIMEOUT 20
    )
    if(NOT interpreter_status EQUAL 0 OR
       NOT "${interpreter_output}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${name}: interpreter result mismatch (${interpreter_status})\n"
            "expected: [${expected}]\nactual: [${interpreter_output}]\n"
            "${interpreter_error}")
    endif()

    if(NOT WIN32)
        return()
    endif()

    set(executable "${directory}/struct-test.exe")
    execute_process(
        COMMAND "${ZP}" build main.zp -o "${executable}"
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE build_status
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 30
    )
    if(NOT build_status EQUAL 0)
        message(FATAL_ERROR "${name}: native build failed (${build_status})\n${build_output}${build_error}")
    endif()
    execute_process(
        COMMAND "${executable}"
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE native_status
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 20
    )
    if(NOT native_status EQUAL 0 OR
       NOT "${native_output}" STREQUAL "${interpreter_output}")
        message(FATAL_ERROR
            "${name}: native/interpreter mismatch (${native_status})\n"
            "interpreter: [${interpreter_output}]\n"
            "native: [${native_output}]\n${native_error}")
    endif()
endfunction()

function(check_failure name expected)
    execute_process(
        COMMAND "${ZP}" check main.zp --diagnostics=json
        WORKING_DIRECTORY "${WORK_DIR}/${name}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 20
    )
    set(diagnostics "${output}\n${error}")
    if(status EQUAL 0 OR NOT diagnostics MATCHES "${expected}")
        message(FATAL_ERROR
            "${name}: expected diagnostic matching [${expected}], got ${status}\n"
            "${diagnostics}")
    endif()
endfunction()

set(basic_dir "${WORK_DIR}/basic nested and managed")
file(MAKE_DIRECTORY "${basic_dir}")
file(WRITE "${basic_dir}/main.zp" [=[
use zp.memory.management
start {
    struct Vec2 {
        x: number
        y: number = 2
    } end struct
    struct Metadata {
        title: string
        enabled: boolean
        tag: char
    } end struct
    struct Shape {
        position: Vec2
        metadata: Metadata
    } end struct
    struct Collection {
        points: Vec2[]
    } end struct
    struct Defaults {
        title = "base"
        enabled = true
    } end struct
    struct TreeNode {
        value: number
        children: TreeNode[]
    } end struct

    fn identity(shape: Shape): Shape {
        return shape
    } end fn

    let position = Vec2(1)
    position.x = 10
    let alias = position
    alias.y = 20
    say(position.x)

    let assignedLater: Vec2
    assignedLater = Vec2(5, 6)
    say(assignedLater.y)
    say(position.y)

    let independent = Vec2(1, 2)
    independent.x = 99
    say(independent.x)
    say(position.x)

    let shape = Shape(Vec2(3, 4), Metadata("ok", true, 'z'))
    let shapeAlias = identity(shape)
    shapeAlias.position.x = 7
    say(shape.position.x)
    shapeAlias.metadata.enabled = false
    say(shape.metadata.enabled)
    say(shape.metadata.tag)

    let other = Vec2(0)
    shape.position = other
    shape.position.y = 8
    say(other.y)

    let collection = Collection([position])
    collection.points[0].x = 13
    say(position.x)

    let defaults: Defaults
    defaults.title = "updated"
    say(defaults.title)
    say(defaults.enabled)

    let root = TreeNode(1, [])
    let leaf = TreeNode(2, [])
    push(root.children, leaf)
    say(root.children[0].value)
    root.children[0].value = 9
    say(leaf.value)

    memory.setThreshold(1024)
    for (let i: number = 0; i < 300; i++) {
        let transient = Shape(Vec2(i, i), Metadata("temporary", true, 't'))
    } end for
    say(shape.metadata.title)
} stop
]=])
check_success("basic nested and managed")
run_parity("basic nested and managed" "10\n6\n20\n99\n10\n7\nfalse\nz\n8\n13\nupdated\ntrue\n2\n9\nok\n")

set(import_dir "${WORK_DIR}/imported visibility")
file(MAKE_DIRECTORY "${import_dir}")
write_module("${import_dir}/models.zh" [=[
pub struct Record:
    value: number
end
struct Secret:
    value: number
end
]=])
file(WRITE "${import_dir}/main.zp" [=[
use models.zh as model
start:
    let record: Record = model.Record(42)
    say(record.value)
stop
]=])
check_success("imported visibility")
run_parity("imported visibility" "42\n")

function(write_invalid name source)
    set(directory "${WORK_DIR}/${name}")
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${directory}/main.zp" "start {\n${source}\n} stop\n")
endfunction()

write_invalid("unknown field type" [=[
    struct Invalid {
        value: MissingType
    } end struct
]=])
check_failure("unknown field type" "unknown type 'MissingType'")

write_invalid("unknown array element type" [=[
    struct Invalid {
        values: MissingType[]
    } end struct
]=])
check_failure("unknown array element type" "unknown type 'MissingType\\[\\]'")

write_invalid("missing field type" [=[
    struct Invalid {
        value
    } end struct
]=])
check_failure("missing field type" "requires a type or default value")

write_invalid("mismatched default" [=[
    struct Invalid {
        value: number = "wrong"
    } end struct
]=])
check_failure("mismatched default" "default value for field 'value' has type 'string'")

write_invalid("required after default" [=[
    struct Invalid {
        optional: number = 1
        required: string
    } end struct
]=])
check_failure("required after default" "required fields must precede fields with default values")

write_invalid("duplicate fields" [=[
    struct Invalid {
        value: number
        value: string
    } end struct
]=])
check_failure("duplicate fields" "duplicate or empty field name in struct")

write_invalid("duplicate structs" [=[
    struct Duplicate { first: number } end struct
    struct Duplicate { second: string } end struct
]=])
check_failure("duplicate structs" "duplicate declaration 'Duplicate' in start block")

write_invalid("uninitialized field access" [=[
    struct Required { value: number } end struct
    let item: Required
    say(item.value)
]=])
execute_process(
    COMMAND "${ZP}" run main.zp
    WORKING_DIRECTORY "${WORK_DIR}/uninitialized field access"
    RESULT_VARIABLE runtime_status
    OUTPUT_VARIABLE runtime_output
    ERROR_VARIABLE runtime_error
    TIMEOUT 20
)
if(runtime_status EQUAL 0 OR
   NOT "${runtime_output}${runtime_error}" MATCHES "member access requires a struct value")
    message(FATAL_ERROR "uninitialized struct access was not rejected safely\n${runtime_output}${runtime_error}")
endif()
if(WIN32)
set(uninitialized_native "${WORK_DIR}/uninitialized field access/uninitialized-native.exe")
execute_process(
    COMMAND "${ZP}" build main.zp -o "${uninitialized_native}"
    WORKING_DIRECTORY "${WORK_DIR}/uninitialized field access"
    RESULT_VARIABLE native_build_status
    OUTPUT_VARIABLE native_build_output
    ERROR_VARIABLE native_build_error
    TIMEOUT 30
)
if(NOT native_build_status EQUAL 0)
    message(FATAL_ERROR "uninitialized struct native fixture failed to build\n${native_build_output}${native_build_error}")
endif()
execute_process(
    COMMAND "${uninitialized_native}"
    WORKING_DIRECTORY "${WORK_DIR}/uninitialized field access"
    RESULT_VARIABLE native_runtime_status
    OUTPUT_VARIABLE native_runtime_output
    ERROR_VARIABLE native_runtime_error
    TIMEOUT 20
)
if(native_runtime_status EQUAL 0 OR
   NOT "${native_runtime_output}${native_runtime_error}" MATCHES "struct")
    message(FATAL_ERROR "native uninitialized struct access did not fail safely\n${native_runtime_output}${native_runtime_error}")
endif()
endif()

write_invalid("wrong constructor type" [=[
    struct Point {
        x: number
        label: string
    } end struct
    let point = Point("wrong", "value")
]=])
check_failure("wrong constructor type" "constructor argument for field 1")

write_invalid("wrong field assignment" [=[
    struct Point {
        x: number
    } end struct
    let point = Point(1)
    point.x = "wrong"
]=])
check_failure("wrong field assignment" "cannot assign 'string' to 'number'")

write_invalid("unknown field access" [=[
    struct Point {
        x: number
    } end struct
    let point = Point(1)
    say(point.missing)
]=])
check_failure("unknown field access" "struct 'Point' has no field 'missing'")

write_invalid("incompatible struct assignment" [=[
    struct Point {
        x: number
    } end struct
    struct Label {
        text: string
    } end struct
    let point = Point(1)
    let label = Label("bad")
    point = label
]=])
check_failure("incompatible struct assignment" "cannot assign 'Label' to 'Point'")

set(private_dir "${WORK_DIR}/private imported struct")
file(MAKE_DIRECTORY "${private_dir}")
write_module("${private_dir}/models.zh" [=[
struct Secret:
    value: number
end
]=])
file(WRITE "${private_dir}/main.zp" [=[
use models.zh as model
start:
    let secret = model.Secret(1)
stop
]=])
check_failure("private imported struct" "struct 'Secret' is private to module 'model'")

set(private_annotation_dir "${WORK_DIR}/private imported struct type annotation")
file(MAKE_DIRECTORY "${private_annotation_dir}")
file(COPY_FILE "${private_dir}/models.zh" "${private_annotation_dir}/models.zh")
file(WRITE "${private_annotation_dir}/main.zp" [=[
use models.zh as model
start:
    let secret: Secret
stop
]=])
check_failure("private imported struct type annotation" "type 'Secret' is private to module 'model'")

message(STATUS "Struct support checks passed")
