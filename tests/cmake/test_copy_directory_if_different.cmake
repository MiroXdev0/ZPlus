if(NOT DEFINED COPY_HELPER OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COPY_HELPER and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
set(source_dir "${WORK_DIR}/source with spaces")
set(destination_dir "${WORK_DIR}/destination with spaces")
file(MAKE_DIRECTORY "${source_dir}/nested")
file(WRITE "${source_dir}/first.txt" "initial")
file(WRITE "${source_dir}/nested/second.txt" "nested")

function(copy_resources)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${source_dir}"
            "-DDESTINATION_DIR=${destination_dir}"
            -P "${COPY_HELPER}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "resource copy failed\n${stdout}${stderr}")
    endif()
endfunction()

copy_resources()
foreach(relative_path IN ITEMS "first.txt" "nested/second.txt")
    if(NOT EXISTS "${destination_dir}/${relative_path}")
        message(FATAL_ERROR "resource was not copied: ${relative_path}")
    endif()
endforeach()

copy_resources()
file(WRITE "${source_dir}/first.txt" "updated")
file(WRITE "${source_dir}/third.txt" "new")
copy_resources()
file(READ "${destination_dir}/first.txt" first_contents)
if(NOT first_contents STREQUAL "updated")
    message(FATAL_ERROR "modified resource was not updated")
endif()
if(NOT EXISTS "${destination_dir}/third.txt")
    message(FATAL_ERROR "new resource was not copied")
endif()

file(REMOVE "${source_dir}/nested/second.txt")
copy_resources()
if(EXISTS "${destination_dir}/nested/second.txt")
    message(FATAL_ERROR "stale resource was not removed")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
