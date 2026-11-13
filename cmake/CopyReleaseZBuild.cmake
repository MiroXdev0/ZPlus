if(NOT CONFIG STREQUAL "Release")
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CopyDirectoryIfDifferent.cmake")

if(NOT DEFINED SOURCE_FILE OR NOT EXISTS "${SOURCE_FILE}")
    message(FATAL_ERROR "Release zbuild executable is missing: ${SOURCE_FILE}")
endif()
if(NOT DEFINED DESTINATION_DIR)
    message(FATAL_ERROR "DESTINATION_DIR must be provided")
endif()
if(NOT DEFINED TEMPLATE_SOURCE_DIR OR NOT IS_DIRECTORY "${TEMPLATE_SOURCE_DIR}")
    message(FATAL_ERROR "TEMPLATE_SOURCE_DIR must name the built-in template directory")
endif()
if(NOT DEFINED STDLIB_SOURCE_DIR OR NOT IS_DIRECTORY "${STDLIB_SOURCE_DIR}")
    message(FATAL_ERROR "STDLIB_SOURCE_DIR must name the standard library directory")
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

copy_directory_if_different("${TEMPLATE_SOURCE_DIR}" "${DESTINATION_DIR}/zbuild-templates")
copy_directory_if_different("${STDLIB_SOURCE_DIR}" "${DESTINATION_DIR}/stdlib")
