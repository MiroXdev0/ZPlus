if(NOT DEFINED ZBUILD OR NOT DEFINED ZP OR
   NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, ZP, SOURCE_DIR, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(COPY "${SOURCE_DIR}/tests/zbuild/physics_runtime/"
    DESTINATION "${WORK_DIR}")

file(TO_CMAKE_PATH "${ZP}" compiler_path)
file(READ "${WORK_DIR}/zproject.toml" project_config)
string(REPLACE "[compiler]" "[compiler]\npath = \"${compiler_path}\""
    project_config "${project_config}")
file(WRITE "${WORK_DIR}/zproject.toml" "${project_config}")

execute_process(
    COMMAND "${ZBUILD}" build
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
    TIMEOUT 90
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "ZBuild physics build failed\n${build_stdout}${build_stderr}")
endif()
if(WIN32)
    set(runtime "${WORK_DIR}/build/release/bin/zphysics_runtime.dll")
    set(executable "${WORK_DIR}/build/release/bin/app.exe")
    set(package_runtime "${WORK_DIR}/build/release/package/zphysics_runtime.dll")
else()
    set(runtime "${WORK_DIR}/build/release/bin/libzphysics_runtime.so")
    set(executable "${WORK_DIR}/build/release/bin/app.out")
    set(package_runtime "${WORK_DIR}/build/release/package/libzphysics_runtime.so")
endif()
if(NOT EXISTS "${runtime}" OR NOT EXISTS "${executable}")
    message(FATAL_ERROR "ZBuild output is missing the physics runtime or executable")
endif()

execute_process(
    COMMAND "${executable}"
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
    TIMEOUT 30
)
if(NOT run_result EQUAL 0 OR
   NOT run_stdout MATCHES "ZBuild physics integration passed")
    message(FATAL_ERROR "ZBuild physics executable failed\n${run_stdout}${run_stderr}")
endif()

execute_process(
    COMMAND "${ZBUILD}" package
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr
    TIMEOUT 90
)
if(NOT package_result EQUAL 0 OR NOT EXISTS "${package_runtime}")
    message(FATAL_ERROR "ZBuild package is missing the physics runtime\n${package_stdout}${package_stderr}")
endif()
message(STATUS "ZBuild physics integration passed\n${run_stdout}${run_stderr}")
