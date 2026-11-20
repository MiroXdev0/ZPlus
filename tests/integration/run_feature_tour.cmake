if(NOT DEFINED ZP OR NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(program "${SOURCE_DIR}/tests/integration/feature_tour.zp")
set(argument "feature-tour-argument")

execute_process(
    COMMAND "${ZP}" check "${program}"
    RESULT_VARIABLE check_result
    OUTPUT_VARIABLE check_output
    ERROR_VARIABLE check_error
    TIMEOUT 30
)
if(NOT check_result EQUAL 0)
    message(FATAL_ERROR "feature tour failed checking\n${check_output}${check_error}")
endif()

execute_process(
    COMMAND "${ZP}" run "${program}" -- "${argument}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE interpreter_result
    OUTPUT_VARIABLE interpreter_output
    ERROR_VARIABLE interpreter_error
    TIMEOUT 30
)
if(NOT interpreter_result EQUAL 0)
    message(FATAL_ERROR
        "feature tour failed in interpreter mode (${interpreter_result})\n${interpreter_output}${interpreter_error}")
endif()
foreach(marker IN ITEMS
    "Z\\+ feature tour complete"
    "remove: ok"
    "random: ok"
    "logging: ok"
    "memory: ok"
    "outer catch:"
    "class: ok"
    "enum: ok"
    "namespace: ok")
    if(NOT "${interpreter_output}" MATCHES "${marker}")
        message(FATAL_ERROR
            "feature tour is missing expected marker [${marker}]\n${interpreter_output}${interpreter_error}")
    endif()
endforeach()

if(HOST_WINDOWS)
    set(native_executable "${WORK_DIR}/feature-tour.exe")
    execute_process(
        COMMAND "${ZP}" build "${program}" -o "${native_executable}"
        RESULT_VARIABLE build_result
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error
        TIMEOUT 60
    )
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "feature tour failed building natively\n${build_output}${build_error}")
    endif()

    execute_process(
        COMMAND "${native_executable}" "${argument}"
        WORKING_DIRECTORY "${WORK_DIR}"
        RESULT_VARIABLE native_result
        OUTPUT_VARIABLE native_output
        ERROR_VARIABLE native_error
        TIMEOUT 30
    )
    if(NOT native_result EQUAL 0)
        message(FATAL_ERROR
            "feature tour failed in native mode (${native_result})\n${native_output}${native_error}")
    endif()
    if(NOT "${native_output}" STREQUAL "${interpreter_output}")
        message(FATAL_ERROR
            "interpreter/native feature tour output differs\ninterpreter:\n${interpreter_output}\nnative:\n${native_output}${native_error}")
    endif()
else()
    message(STATUS "Native feature-tour parity is Windows x64-only; interpreter run passed")
endif()
