if(NOT DEFINED SQLITE_TEST)
    message(FATAL_ERROR "SQLITE_TEST must be provided")
endif()

execute_process(
    COMMAND "${SQLITE_TEST}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 20
)
if(NOT result EQUAL 0 OR NOT "${output}" STREQUAL "SQLite runtime tests passed\n")
    message(FATAL_ERROR "SQLite runtime tests failed (${result})\n${output}${error}")
endif()
