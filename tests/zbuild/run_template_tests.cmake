if(NOT DEFINED ZBUILD OR NOT DEFINED ZP OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZBUILD, ZP, and WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(custom_root "${WORK_DIR}/custom-templates")
set(custom_template "${custom_root}/HelloCustom")
file(MAKE_DIRECTORY "${custom_template}/src")
file(WRITE "${custom_template}/template.toml"
"name = \"HelloCustom\"\n"
"description = \"Custom template used by ZBuild integration tests.\"\n"
"version = \"1.0.0\"\n"
"required_zplus_version = \"0.5.0\"\n"
"default_target_type = \"executable\"\n"
"platforms = [\"Windows\", \"Linux\"]\n"
"required_modules = []\n"
"aliases = [\"hello\"]\n"
"configuration = [\"backend=interpreter\"]\n"
"directories = [\"src\"]\n"
"files = [\"zproject.toml\", \"README.md\", \"src/main.zp\"]\n")
file(WRITE "${custom_template}/zproject.toml"
"[project]\n"
"name = \"{{PROJECT_NAME}}\"\n"
"version = \"{{PROJECT_VERSION}}\"\n"
"description = \"Created in {{YEAR}} by {{AUTHOR}}\"\n"
"entry = \"src/main.zp\"\n"
"\n[build]\nbackend = \"{{BUILD_BACKEND}}\"\nmode = \"{{BUILD_MODE}}\"\n"
"output_dir = \"build\"\noptimization = 0\njobs = 0\n"
"\n[compiler]\nwarnings = true\ndebug_symbols = false\n"
"\n[platform]\nwindows = true\nlinux = true\n")
file(WRITE "${custom_template}/README.md"
"# {{PROJECT_NAME}}\n"
"Created in {{YEAR}} by {{AUTHOR}}.\n")
file(WRITE "${custom_template}/src/main.zp"
"start:\n    printz(\"Hello custom template\\n\")\nstop\n")

set(shadowed_game "${custom_root}/Game")
file(MAKE_DIRECTORY "${shadowed_game}/src")
file(WRITE "${shadowed_game}/template.toml"
"name = \"Game\"\ndescription = \"Shadowed custom Game template.\"\n"
"version = \"1.0.0\"\nrequired_zplus_version = \"0.5.0\"\n"
"default_target_type = \"executable\"\nfiles = [\"src/main.zp\"]\n")
file(WRITE "${shadowed_game}/src/main.zp" "start:\nstop\n")

function(run_zbuild expected_result working_directory)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "ZBUILD_TEMPLATES_DIR=${custom_root}"
            "${ZBUILD}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 90
    )
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR
            "zbuild ${ARGN} returned ${result}, expected ${expected_result}\n${stdout}${stderr}")
    endif()
    set(last_output "${stdout}${stderr}" PARENT_SCOPE)
endfunction()

run_zbuild(0 "${WORK_DIR}" templates)
if(NOT last_output MATCHES "shadowed by a built-in template")
    message(FATAL_ERROR "built-in template precedence was not reported\n${last_output}")
endif()
foreach(required_template IN ITEMS Game WebServer Console Library Desktop Empty HelloCustom)
    if(NOT last_output MATCHES "${required_template}")
        message(FATAL_ERROR "template listing is missing ${required_template}\n${last_output}")
    endif()
endforeach()
run_zbuild(0 "${WORK_DIR}" template info gAmE)
if(NOT last_output MATCHES "zgraphics" OR NOT last_output MATCHES "Windows")
    message(FATAL_ERROR "Game template information is incomplete\n${last_output}")
endif()
run_zbuild(0 "${WORK_DIR}" template info server)
if(NOT last_output MATCHES "[Uu]navailable")
    message(FATAL_ERROR "WebServer limitation was not reported\n${last_output}")
endif()
run_zbuild(1 "${WORK_DIR}" init NoSuchTemplate --template Nope)
if(NOT last_output MATCHES "Available templates" OR NOT last_output MATCHES "Game")
    message(FATAL_ERROR "unknown template error did not list alternatives\n${last_output}")
endif()
run_zbuild(1 "${WORK_DIR}" init WebProject --template WebServer)
if(NOT last_output MATCHES "no HTTP server")
    message(FATAL_ERROR "unavailable WebServer template did not explain why\n${last_output}")
endif()
if(EXISTS "${WORK_DIR}/WebProject")
    message(FATAL_ERROR "unavailable template created a project directory")
endif()

set(template_args Game "-template" "Game" Desktop "--template" "Desktop"
    Console "-template" "console" Library "--template" "lib"
    Empty "-template" "minimal")
while(template_args)
    list(POP_FRONT template_args project_name)
    list(POP_FRONT template_args option)
    list(POP_FRONT template_args template_name)
    run_zbuild(0 "${WORK_DIR}" init "${project_name}" "${option}" "${template_name}")
    set(project_dir "${WORK_DIR}/${project_name}")
    if(NOT EXISTS "${project_dir}/zproject.toml" OR
       NOT EXISTS "${project_dir}/README.md")
        message(FATAL_ERROR "${project_name} template omitted required project files")
    endif()
    file(READ "${project_dir}/zproject.toml" generated_config)
    if(NOT generated_config MATCHES "name = \"${project_name}\"")
        message(FATAL_ERROR "${project_name} was not substituted into project configuration")
    endif()
    if(project_name STREQUAL "Game" AND NOT EXISTS "${project_dir}/src/game/player.zh")
        message(FATAL_ERROR "custom template unexpectedly overrode the built-in Game template")
    endif()
    if(project_name STREQUAL "Empty")
        file(GLOB_RECURSE initial_files LIST_DIRECTORIES false "${project_dir}/*")
        list(LENGTH initial_files initial_file_count)
        if(NOT initial_file_count EQUAL 3)
            message(FATAL_ERROR "Empty template generated unnecessary files: ${initial_files}")
        endif()
    endif()
    run_zbuild(0 "${project_dir}" configure)
    file(GLOB_RECURSE generated_files LIST_DIRECTORIES false "${project_dir}/*")
    foreach(generated_file IN LISTS generated_files)
        file(READ "${generated_file}" generated_contents)
        if(generated_contents MATCHES "\\{\\{")
            message(FATAL_ERROR "unresolved template variable remains in ${generated_file}")
        endif()
    endforeach()
endwhile()

foreach(generated_project IN ITEMS Game Desktop Console Empty)
    run_zbuild(0 "${WORK_DIR}/${generated_project}" build)
endforeach()
run_zbuild(0 "${WORK_DIR}/Console" run -- Ada)
if(NOT last_output MATCHES "Hello, Ada!")
    message(FATAL_ERROR "Console template did not pass command-line arguments\n${last_output}")
endif()
run_zbuild(0 "${WORK_DIR}/Library" test --verbose)
if(NOT last_output MATCHES "Library greeting test passed" OR
   NOT last_output MATCHES "Library utility test passed")
    message(FATAL_ERROR "Library template tests did not pass\n${last_output}")
endif()

set(custom_project "${WORK_DIR}/CustomProject")
run_zbuild(0 "${WORK_DIR}" init CustomProject --template hello
    --version 2.3.4 --author "Template Tester")
if(NOT EXISTS "${custom_project}/zproject.toml")
    message(FATAL_ERROR "custom template was not generated")
endif()
file(READ "${custom_project}/zproject.toml" custom_config)
if(NOT custom_config MATCHES "version = \"2.3.4\"" OR
   NOT custom_config MATCHES "Created in [0-9][0-9][0-9][0-9] by Template Tester")
    message(FATAL_ERROR "custom template variables were not substituted")
endif()
run_zbuild(0 "${custom_project}" run)
if(NOT last_output MATCHES "Hello custom template")
    message(FATAL_ERROR "custom template program failed\n${last_output}")
endif()

set(current_directory "${WORK_DIR}/current directory")
file(MAKE_DIRECTORY "${current_directory}")
run_zbuild(0 "${current_directory}" init -template Empty)
file(READ "${current_directory}/zproject.toml" current_config)
if(NOT current_config MATCHES "name = \"current_directory\"")
    message(FATAL_ERROR "initialization in the current directory failed to infer a safe project name")
endif()

set(no_overwrite "${WORK_DIR}/NoOverwrite")
file(MAKE_DIRECTORY "${no_overwrite}")
file(WRITE "${no_overwrite}/README.md" "keep me\n")
run_zbuild(1 "${WORK_DIR}" init NoOverwrite --template Empty)
file(READ "${no_overwrite}/README.md" preserved_readme)
if(NOT preserved_readme STREQUAL "keep me\n" OR EXISTS "${no_overwrite}/zproject.toml")
    message(FATAL_ERROR "initialization modified files after an overwrite conflict")
endif()

set(duplicate_template "${custom_root}/Duplicate")
file(MAKE_DIRECTORY "${duplicate_template}/src")
file(WRITE "${duplicate_template}/template.toml"
"name = \"HelloCustom\"\ndescription = \"Duplicate custom template.\"\n"
"version = \"1.0.0\"\nrequired_zplus_version = \"0.5.0\"\n"
"default_target_type = \"executable\"\nfiles = [\"src/main.zp\"]\n")
file(WRITE "${duplicate_template}/src/main.zp" "start:\nstop\n")
run_zbuild(1 "${WORK_DIR}" templates)
if(NOT last_output MATCHES "duplicate custom template")
    message(FATAL_ERROR "duplicate custom templates were not diagnosed\n${last_output}")
endif()
file(REMOVE_RECURSE "${duplicate_template}")

set(invalid_path_template "${custom_root}/InvalidPath")
file(MAKE_DIRECTORY "${invalid_path_template}")
file(WRITE "${invalid_path_template}/template.toml"
"name = \"InvalidPath\"\ndescription = \"Unsafe path test.\"\n"
"version = \"1.0.0\"\nrequired_zplus_version = \"0.5.0\"\n"
"default_target_type = \"executable\"\nfiles = [\"../outside.zp\"]\n")
run_zbuild(1 "${WORK_DIR}" templates)
if(NOT last_output MATCHES "unsafe file path")
    message(FATAL_ERROR "unsafe template paths were not rejected\n${last_output}")
endif()
file(REMOVE_RECURSE "${invalid_path_template}")

set(missing_file_template "${custom_root}/MissingFile")
file(MAKE_DIRECTORY "${missing_file_template}")
file(WRITE "${missing_file_template}/template.toml"
"name = \"MissingFile\"\ndescription = \"Missing template file test.\"\n"
"version = \"1.0.0\"\nrequired_zplus_version = \"0.5.0\"\n"
"default_target_type = \"executable\"\nfiles = [\"src/main.zp\"]\n")
run_zbuild(1 "${WORK_DIR}" templates)
if(NOT last_output MATCHES "missing or unsafe file")
    message(FATAL_ERROR "missing template files were not diagnosed\n${last_output}")
endif()
file(REMOVE_RECURSE "${missing_file_template}")

set(unresolved_template "${custom_root}/Unresolved")
file(MAKE_DIRECTORY "${unresolved_template}/src")
file(WRITE "${unresolved_template}/template.toml"
"name = \"Unresolved\"\ndescription = \"Unresolved placeholder test.\"\n"
"version = \"1.0.0\"\nrequired_zplus_version = \"0.5.0\"\n"
"default_target_type = \"executable\"\nfiles = [\"src/main.zp\"]\n")
file(WRITE "${unresolved_template}/src/main.zp"
"start:\n    printz(\"{{MISSING}}\")\nstop\n")
run_zbuild(1 "${WORK_DIR}" init UnresolvedProject --template Unresolved)
if(NOT last_output MATCHES "unresolved or invalid variable")
    message(FATAL_ERROR "unresolved template variables were not diagnosed\n${last_output}")
endif()
if(EXISTS "${WORK_DIR}/UnresolvedProject")
    message(FATAL_ERROR "failed template generation left a partial project")
endif()

message(STATUS "ZBuild template tests passed")
