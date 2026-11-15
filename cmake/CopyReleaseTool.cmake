if(NOT CONFIG STREQUAL "Release")
    return()
endif()

if(NOT DEFINED SOURCE_FILE OR NOT EXISTS "${SOURCE_FILE}")
    message(FATAL_ERROR "Release tool executable is missing: ${SOURCE_FILE}")
endif()
if(NOT DEFINED DESTINATION_DIR)
    message(FATAL_ERROR "DESTINATION_DIR must be provided")
endif()

file(MAKE_DIRECTORY "${DESTINATION_DIR}")
get_filename_component(executable_name "${SOURCE_FILE}" NAME)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SOURCE_FILE}" "${DESTINATION_DIR}/${executable_name}"
    RESULT_VARIABLE copy_result
)
if(NOT copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to install Release ${executable_name}")
endif()
