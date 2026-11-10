if(NOT CONFIG STREQUAL "Release")
    return()
endif()

if(NOT EXISTS "${SOURCE_FILE}")
    message(FATAL_ERROR "Release compiler executable was not produced: ${SOURCE_FILE}")
endif()

file(MAKE_DIRECTORY "${DESTINATION_DIR}")

function(install_release_file source destination label)
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "Release ${label} was not produced: ${source}")
    endif()

    if(EXISTS "${destination}")
        file(SHA256 "${source}" source_hash)
        file(SHA256 "${destination}" destination_hash)
        if(source_hash STREQUAL destination_hash)
            return()
        endif()
    endif()

    set(temporary "${destination}.release-tmp")
    configure_file("${source}" "${temporary}" COPYONLY)
    if(UNIX)
        file(GET_PERMISSIONS "${source}" source_permissions)
        file(CHMOD "${temporary}" PERMISSIONS ${source_permissions})
    endif()
    file(RENAME "${temporary}" "${destination}" RESULT rename_result)

    if(NOT rename_result STREQUAL "0")
        file(REMOVE "${temporary}")
        message(FATAL_ERROR "Failed to install Release ${label}: ${rename_result}")
    endif()

    if(NOT EXISTS "${destination}")
        message(FATAL_ERROR "Release ${label} install is missing: ${destination}")
    endif()
endfunction()

get_filename_component(executable_name "${SOURCE_FILE}" NAME)
install_release_file(
    "${SOURCE_FILE}"
    "${DESTINATION_DIR}/${executable_name}"
    "compiler executable"
)

if(DEFINED RUNTIME_FILE AND NOT RUNTIME_FILE STREQUAL "")
    get_filename_component(runtime_name "${RUNTIME_FILE}" NAME)
    install_release_file(
        "${RUNTIME_FILE}"
        "${DESTINATION_DIR}/${runtime_name}"
        "ZGraphics runtime"
    )
endif()

if(DEFINED SQLITE_RUNTIME_FILE AND NOT SQLITE_RUNTIME_FILE STREQUAL "")
    install_release_file(
        "${SQLITE_RUNTIME_FILE}"
        "${DESTINATION_DIR}/sqlite3.dll"
        "SQLite runtime"
    )
endif()

if(DEFINED AUDIO_RUNTIME_FILE AND NOT AUDIO_RUNTIME_FILE STREQUAL "")
    get_filename_component(audio_runtime_name "${AUDIO_RUNTIME_FILE}" NAME)
    install_release_file(
        "${AUDIO_RUNTIME_FILE}"
        "${DESTINATION_DIR}/${audio_runtime_name}"
        "audio runtime"
    )
endif()

if(DEFINED PHYSICS_RUNTIME_FILE AND NOT PHYSICS_RUNTIME_FILE STREQUAL "")
    get_filename_component(physics_runtime_name "${PHYSICS_RUNTIME_FILE}" NAME)
    install_release_file(
        "${PHYSICS_RUNTIME_FILE}"
        "${DESTINATION_DIR}/${physics_runtime_name}"
        "physics runtime"
    )
endif()
