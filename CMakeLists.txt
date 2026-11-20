
cmake_minimum_required(VERSION 3.20)

project(ZPlus
    VERSION 1.3.3
    LANGUAGES CXX
)

set(ZBUILD_VERSION "1.0.0")
set(ZPM_VERSION "1.0.0")

include(CTest)

set(ZPLUS_RELEASE_BIN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Destination for the Release compiler executable and runtime")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
# Only fetch headers/source; the runtime builds its configured miniaudio TU itself.
cmake_policy(PUSH)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.26
    GIT_SHALLOW TRUE
)
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()
cmake_policy(POP)
FetchContent_MakeAvailable(box2d)

add_library(zbuild_engine STATIC
    zbuild/Build.cpp
    zbuild/BuildGraph.cpp
    zbuild/Cache.cpp
    zbuild/Config.cpp
    zbuild/Dependencies.cpp
    zbuild/Filesystem.cpp
    zbuild/Hash.cpp
    zbuild/Packages.cpp
    zbuild/Process.cpp
    zbuild/RegistryClient.cpp
    zbuild/RegistryPackages.cpp
    zbuild/Template.cpp
    runtime/Json.cpp
)
target_compile_definitions(zbuild_engine PRIVATE "ZBUILD_VERSION=\"${ZBUILD_VERSION}\"")
target_compile_definitions(zbuild_engine PRIVATE "ZPM_VERSION=\"${ZPM_VERSION}\"")
target_include_directories(zbuild_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/zbuild
)
if(WIN32)
    target_link_libraries(zbuild_engine PUBLIC winhttp crypt32)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(zbuild_engine PUBLIC CURL::libcurl)
endif()
add_executable(zbuild zbuild/main.cpp)
target_link_libraries(zbuild PRIVATE zbuild_engine)
target_compile_definitions(zbuild PRIVATE "ZBUILD_VERSION=\"${ZBUILD_VERSION}\"")
add_custom_command(TARGET zbuild POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/zbuild/templates"
        "-DDESTINATION_DIR=$<TARGET_FILE_DIR:zbuild>/zbuild-templates"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        "-DDESTINATION_DIR=$<TARGET_FILE_DIR:zbuild>/stdlib"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
    VERBATIM
    COMMENT "Packaging adjacent ZBuild resources"
)
add_custom_command(TARGET zbuild POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DCONFIG=$<CONFIG>"
        "-DSOURCE_FILE=$<TARGET_FILE:zbuild>"
        "-DDESTINATION_DIR=${ZPLUS_RELEASE_BIN_DIR}"
        "-DTEMPLATE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/zbuild/templates"
        "-DSTDLIB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyReleaseZBuild.cmake"
    VERBATIM
    COMMENT "Installing the Release zbuild compatibility executable into bin/"
)
add_executable(zpbuild zbuild/main.cpp)
target_link_libraries(zpbuild PRIVATE zbuild_engine)
target_compile_definitions(zpbuild PRIVATE "ZBUILD_VERSION=\"${ZBUILD_VERSION}\"")
add_custom_command(TARGET zpbuild POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/zbuild/templates"
        "-DDESTINATION_DIR=$<TARGET_FILE_DIR:zpbuild>/zbuild-templates"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        "-DDESTINATION_DIR=$<TARGET_FILE_DIR:zpbuild>/stdlib"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DCONFIG=$<CONFIG>"
        "-DSOURCE_FILE=$<TARGET_FILE:zpbuild>"
        "-DDESTINATION_DIR=${ZPLUS_RELEASE_BIN_DIR}"
        "-DTEMPLATE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/zbuild/templates"
        "-DSTDLIB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyReleaseZBuild.cmake"
    VERBATIM
    COMMENT "Installing the Release ZPBuild CLI into bin/"
)
add_executable(zpm zpm/main.cpp)
target_link_libraries(zpm PRIVATE zbuild_engine)
target_compile_definitions(zpm PRIVATE "ZPM_VERSION=\"${ZPM_VERSION}\"")
add_custom_command(TARGET zpm POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DCONFIG=$<CONFIG>"
        "-DSOURCE_FILE=$<TARGET_FILE:zpm>"
        "-DDESTINATION_DIR=${ZPLUS_RELEASE_BIN_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyReleaseTool.cmake"
    VERBATIM
    COMMENT "Installing the Release ZPM executable into bin/"
)

add_executable(zp
    compiler/main.cpp
    compiler/ModuleLoader.cpp

    compiler/lexer/Lexer.cpp
    compiler/lexer/Token.cpp

    compiler/parser/Parser.cpp
    compiler/parser/AST.cpp

    compiler/semantic/Analyzer.cpp

    compiler/ir/IR.cpp
    compiler/ir/IRBuilder.cpp

    runtime/Runtime.cpp
    runtime/Base64.cpp
    runtime/Json.cpp
    runtime/Http.cpp
    runtime/WebMultipart.cpp
    runtime/WebSessions.cpp
    runtime/WebTemplate.cpp
    runtime/WebValidation.cpp
    runtime/WebMiddleware.cpp
    runtime/Web.cpp
    runtime/SQLite.cpp
    runtime/StandardLibrary.cpp
    runtime/installer/Platform.cpp
)
target_compile_definitions(zp PRIVATE "ZPLUS_VERSION=\"${PROJECT_VERSION}\"")

add_library(zplus_graphics STATIC
    ZGraphics/Core/Window.cpp
    ZGraphics/Rendering/Color.cpp
)

if(WIN32)
    enable_language(RC)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/ZPlus.ico" ZPLUS_ICON_RC_PATH)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/zplus.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/zplus_icon.rc"
        @ONLY
    )
    target_sources(zp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/zplus_icon.rc")
    target_sources(zbuild PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/zplus_icon.rc")
    target_sources(zpbuild PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/zplus_icon.rc")
    function(zplus_add_version_resource target description filename filetype)
        if(ARGC GREATER 4)
            set(ZPLUS_RESOURCE_VERSION "${ARGV4}")
        else()
            set(ZPLUS_RESOURCE_VERSION "${PROJECT_VERSION}")
        endif()
        string(REPLACE "." ";" resource_version_parts "${ZPLUS_RESOURCE_VERSION}")
        list(LENGTH resource_version_parts resource_version_length)
        if(NOT resource_version_length EQUAL 3)
            message(FATAL_ERROR "Version resources require a three-part version: ${ZPLUS_RESOURCE_VERSION}")
        endif()
        list(GET resource_version_parts 0 ZPLUS_RESOURCE_VERSION_MAJOR)
        list(GET resource_version_parts 1 ZPLUS_RESOURCE_VERSION_MINOR)
        list(GET resource_version_parts 2 ZPLUS_RESOURCE_VERSION_PATCH)
        set(ZPLUS_FILE_DESCRIPTION "${description}")
        set(ZPLUS_ORIGINAL_FILENAME "${filename}")
        set(ZPLUS_FILE_TYPE "${filetype}")
        if(target STREQUAL "zbuild" OR target STREQUAL "zpbuild")
            set(ZPLUS_PRODUCT_NAME "ZPBuild")
        elseif(target STREQUAL "zpm")
            set(ZPLUS_PRODUCT_NAME "ZPM")
        else()
            set(ZPLUS_PRODUCT_NAME "Z+ Programming Language")
        endif()
        set(version_resource "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/zplus_version.rc.in"
            "${version_resource}"
            @ONLY
        )
        target_sources(${target} PRIVATE "${version_resource}")
        if(MSVC)
            target_link_options(${target} PRIVATE /Brepro)
        endif()
    endfunction()
    zplus_add_version_resource(zp "Z+ Compiler" "zp.exe" VFT_APP)
    zplus_add_version_resource(
        zbuild "ZPBuild Compatibility CLI" "zbuild.exe" VFT_APP "${ZBUILD_VERSION}")
    zplus_add_version_resource(zpbuild "ZPBuild CLI" "zpbuild.exe" VFT_APP "${ZBUILD_VERSION}")
    zplus_add_version_resource(zpm "ZPM CLI" "zpm.exe" VFT_APP "${ZPM_VERSION}")
    target_sources(zplus_graphics PRIVATE
        ZGraphics/Platform/WindowsWindow.cpp
    )
    target_link_libraries(zplus_graphics PUBLIC user32 gdi32 gdiplus)
else()
    find_package(X11 REQUIRED)
    target_sources(zplus_graphics PRIVATE
        ZGraphics/Platform/X11Window.cpp
    )
    target_include_directories(zplus_graphics PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(zplus_graphics PUBLIC ${X11_LIBRARIES})
endif()

target_include_directories(zplus_graphics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ZGraphics
)
target_link_libraries(zp PRIVATE zplus_graphics)
find_package(Threads REQUIRED)
target_link_libraries(zp PRIVATE Threads::Threads)
target_link_libraries(zbuild_engine PUBLIC Threads::Threads)
add_library(zaudio_engine STATIC
    runtime/audio/AudioEngine.cpp
    runtime/audio/AudioError.cpp
    runtime/audio/AudioDevice.cpp
    runtime/audio/AudioResource.cpp
    runtime/audio/AudioFormat.cpp
    runtime/audio/AudioPlayback.cpp
    runtime/audio/AudioProperties.cpp
    runtime/audio/AudioControl.cpp
    runtime/audio/StbVorbis.cpp
    runtime/audio/Miniaudio.cpp
)
set_target_properties(zaudio_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(zaudio_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/audio
    ${miniaudio_SOURCE_DIR}
)
target_include_directories(zaudio_engine PRIVATE
    ${miniaudio_SOURCE_DIR}/extras
)
target_link_libraries(zaudio_engine PUBLIC Threads::Threads)
if(CMAKE_DL_LIBS)
    target_link_libraries(zaudio_engine PUBLIC ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_link_libraries(zaudio_engine PUBLIC ole32 winmm avrt)
endif()
target_link_libraries(zp PRIVATE zaudio_engine)

add_library(zaudio_runtime SHARED runtime/audio/RuntimeApi.cpp)
target_include_directories(zaudio_runtime PRIVATE runtime/audio)
target_link_libraries(zaudio_runtime PRIVATE zaudio_engine)
target_compile_definitions(zaudio_runtime PRIVATE ZAUDIO_RUNTIME_EXPORTS)
set_target_properties(zaudio_runtime PROPERTIES OUTPUT_NAME zaudio_runtime)
if(WIN32)
    zplus_add_version_resource(zaudio_runtime "Z+ Audio Runtime" "zaudio_runtime.dll" VFT_DLL)
endif()
add_dependencies(zp zaudio_runtime)

set(ZPLUS_AUDIO_RUNTIME_ARGUMENT
    "-DAUDIO_RUNTIME_FILE=$<TARGET_FILE:zaudio_runtime>"
)

add_library(zphysics_engine STATIC
    runtime/physics/PhysicsWorld.cpp
    runtime/physics/PhysicsBody.cpp
    runtime/physics/PhysicsShape.cpp
    runtime/physics/PhysicsJoint.cpp
    runtime/physics/PhysicsQuery.cpp
)
set_target_properties(zphysics_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(zphysics_engine PUBLIC runtime/physics)
target_link_libraries(zphysics_engine PUBLIC box2d Threads::Threads)
target_link_libraries(zp PRIVATE zphysics_engine)

add_library(zphysics_runtime SHARED runtime/physics/RuntimeApi.cpp)
target_include_directories(zphysics_runtime PRIVATE runtime/physics)
target_link_libraries(zphysics_runtime PRIVATE zphysics_engine)
target_compile_definitions(zphysics_runtime PRIVATE ZPHYSICS_RUNTIME_EXPORTS)
set_target_properties(zphysics_runtime PROPERTIES OUTPUT_NAME zphysics_runtime)
if(WIN32)
    zplus_add_version_resource(zphysics_runtime "Z+ Physics Runtime" "zphysics_runtime.dll" VFT_DLL)
endif()
add_dependencies(zp zphysics_runtime)

set(ZPLUS_PHYSICS_RUNTIME_ARGUMENT
    "-DPHYSICS_RUNTIME_FILE=$<TARGET_FILE:zphysics_runtime>"
)

add_library(zplus_input_runtime SHARED runtime/InputRuntime.cpp)
target_include_directories(zplus_input_runtime PRIVATE runtime)
set_target_properties(zplus_input_runtime PROPERTIES OUTPUT_NAME zplus_input_runtime)
if(WIN32)
    zplus_add_version_resource(zplus_input_runtime "Z+ Input Runtime" "zplus_input_runtime.dll" VFT_DLL)
endif()
add_dependencies(zp zplus_input_runtime)

set(ZPLUS_INPUT_RUNTIME_ARGUMENT
    "-DINPUT_RUNTIME_FILE=$<TARGET_FILE:zplus_input_runtime>"
)

if(CMAKE_DL_LIBS)
    target_link_libraries(zp PRIVATE ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_link_libraries(zp PRIVATE ws2_32 shell32 ole32 advapi32 bcrypt)
endif()
target_include_directories(zp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    compiler
    runtime
)

add_library(zp_native_backend STATIC
    compiler/backend/native/MachineCode.cpp
    compiler/backend/native/X64CodeGenerator.cpp
    compiler/backend/native/NativeCodeGenerator.cpp
    compiler/backend/native/PEWriter.cpp
    compiler/backend/native/ELFWriter.cpp
    compiler/ir/IRAnalysis.cpp
    compiler/ir/IROptimizer.cpp
    compiler/semantic/BuiltinRegistry.cpp
    compiler/semantic/Type.cpp
    compiler/ir/IRVerifier.cpp
)
target_include_directories(zp_native_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    compiler
)
target_link_libraries(zp PRIVATE zp_native_backend)

if(WIN32)
    set(ZPLUS_SQLITE_RUNTIME_ARGUMENT)
    add_library(zplus_graphics_runtime SHARED
        runtime/graphics/NativeApi.cpp
        runtime/Base64.cpp
        runtime/Json.cpp
        runtime/Http.cpp
        runtime/WebMultipart.cpp
        runtime/WebSessions.cpp
        runtime/WebTemplate.cpp
        runtime/WebValidation.cpp
        runtime/WebMiddleware.cpp
        runtime/Web.cpp
        runtime/SQLite.cpp
        runtime/StandardLibrary.cpp
        runtime/installer/Platform.cpp
    )
    target_compile_definitions(zplus_graphics_runtime PRIVATE ZPLUS_GRAPHICS_RUNTIME_EXPORTS)
    target_include_directories(zplus_graphics_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        runtime
    )
    target_link_libraries(zplus_graphics_runtime PRIVATE
        zplus_graphics shell32 ole32 advapi32 bcrypt
    )
    target_link_libraries(zplus_graphics_runtime PRIVATE Threads::Threads ws2_32)
    find_file(ZPLUS_SQLITE_RUNTIME_FILE
        NAMES sqlite3.dll libsqlite3-0.dll
        HINTS
            "$ENV{VCPKG_ROOT}/installed/x64-windows/bin"
            "$ENV{MSYS2_ROOT}/ucrt64/bin"
            "C:/msys64/ucrt64/bin"
    )
    if(ZPLUS_SQLITE_RUNTIME_FILE)
        add_custom_command(TARGET zplus_graphics_runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ZPLUS_SQLITE_RUNTIME_FILE}"
                "$<TARGET_FILE_DIR:zplus_graphics_runtime>/sqlite3.dll"
            VERBATIM
            COMMENT "Packaging the SQLite runtime beside the native runtime"
        )
        set(ZPLUS_SQLITE_RUNTIME_ARGUMENT
            "-DSQLITE_RUNTIME_FILE=${ZPLUS_SQLITE_RUNTIME_FILE}"
        )
    endif()
    set_target_properties(zplus_graphics_runtime PROPERTIES
        OUTPUT_NAME zgraphics_runtime
    )
    zplus_add_version_resource(
        zplus_graphics_runtime "Z+ Graphics Runtime" "zgraphics_runtime.dll" VFT_DLL)
    add_dependencies(zp zplus_graphics_runtime zaudio_runtime zphysics_runtime)
    set(ZPLUS_GRAPHICS_RUNTIME_ARGUMENT
        "-DRUNTIME_FILE=$<TARGET_FILE:zplus_graphics_runtime>"
        "-DGRAPHICS_RUNTIME=$<TARGET_FILE:zplus_graphics_runtime>"
    )
    add_custom_command(TARGET zp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:zplus_graphics_runtime>"
            "$<TARGET_FILE_DIR:zp>"
        VERBATIM
        COMMENT "Packaging the ZGraphics native runtime beside zp"
    )
    if(ZPLUS_SQLITE_RUNTIME_FILE)
        add_custom_command(TARGET zp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ZPLUS_SQLITE_RUNTIME_FILE}"
                "$<TARGET_FILE_DIR:zp>/sqlite3.dll"
            VERBATIM
            COMMENT "Packaging the SQLite runtime beside zp"
        )
    endif()
endif()

add_custom_command(TARGET zp POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        "-DDESTINATION_DIR=$<TARGET_FILE_DIR:zp>/stdlib"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:zplus_input_runtime>"
        "$<TARGET_FILE_DIR:zp>"
    VERBATIM
    COMMENT "Packaging compiler runtime resources beside zp"
)
add_custom_command(TARGET zp POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DCONFIG=$<CONFIG>"
        "-DSOURCE_FILE=$<TARGET_FILE:zp>"
        "-DDESTINATION_DIR=${ZPLUS_RELEASE_BIN_DIR}"
        "-DSTDLIB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
        ${ZPLUS_GRAPHICS_RUNTIME_ARGUMENT}
        ${ZPLUS_SQLITE_RUNTIME_ARGUMENT}
        ${ZPLUS_AUDIO_RUNTIME_ARGUMENT}
        ${ZPLUS_PHYSICS_RUNTIME_ARGUMENT}
        ${ZPLUS_INPUT_RUNTIME_ARGUMENT}
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyReleaseCompiler.cmake"
    VERBATIM
    COMMENT "Installing the Release zp executable into bin/"
)

add_custom_target(zp_release_bin_copy ALL
    COMMAND ${CMAKE_COMMAND}
        "-DCONFIG=$<CONFIG>"
        "-DSOURCE_FILE=$<TARGET_FILE:zp>"
        "-DDESTINATION_DIR=${ZPLUS_RELEASE_BIN_DIR}"
        ${ZPLUS_GRAPHICS_RUNTIME_ARGUMENT}
        ${ZPLUS_SQLITE_RUNTIME_ARGUMENT}
        ${ZPLUS_AUDIO_RUNTIME_ARGUMENT}
        ${ZPLUS_PHYSICS_RUNTIME_ARGUMENT}
        ${ZPLUS_INPUT_RUNTIME_ARGUMENT}
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyReleaseCompiler.cmake"
    DEPENDS zp
    VERBATIM
    COMMENT "Verifying the Release zp executable in bin/"
)

if(MSVC)
    target_compile_options(zp PRIVATE /W4)
    target_compile_options(zbuild_engine PRIVATE /W4)
    target_compile_options(zbuild PRIVATE /W4)
    target_compile_options(zpbuild PRIVATE /W4)
    target_compile_options(zpm PRIVATE /W4)
    target_compile_options(zplus_graphics PRIVATE /W4)
    target_compile_options(zp_native_backend PRIVATE /W4)
    if(WIN32)
        target_compile_options(zplus_graphics_runtime PRIVATE /W4)
    endif()
else()
    target_compile_options(zp PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zbuild_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zbuild PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zpbuild PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zpm PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zplus_graphics PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(zp_native_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(zp_ir_verifier_tests
        tests/compiler/ir_verifier.cpp
        compiler/semantic/Analyzer.cpp
        compiler/parser/AST.cpp
        compiler/ir/IRVerifier.cpp
        compiler/semantic/BuiltinRegistry.cpp
        compiler/semantic/Type.cpp
    )
    target_include_directories(zp_ir_verifier_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        compiler
    )
    add_test(NAME typed_ir_verifier COMMAND zp_ir_verifier_tests)
    add_executable(zp_ir_optimization_tests
        tests/compiler/ir_optimization.cpp
        compiler/ir/IRAnalysis.cpp
        compiler/ir/IROptimizer.cpp
        compiler/ir/IRVerifier.cpp
        compiler/semantic/BuiltinRegistry.cpp
        compiler/semantic/Type.cpp
    )
    target_include_directories(zp_ir_optimization_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        compiler
    )
    add_test(NAME ir_optimization COMMAND zp_ir_optimization_tests)
    add_test(
        NAME generic_language
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/generic-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/compiler/run_generic_tests.cmake"
    )

    add_test(NAME zp_version COMMAND zp version)
    set_tests_properties(zp_version PROPERTIES
        PASS_REGULAR_EXPRESSION "Z[+] Compiler 1\\.3\\.3"
    )
    add_test(NAME zpbuild_version COMMAND zpbuild version)
    set_tests_properties(zpbuild_version PROPERTIES
        PASS_REGULAR_EXPRESSION "ZPBuild 1\\.0\\.0"
    )
    add_test(NAME zbuild_compatibility_version COMMAND zbuild --version)
    set_tests_properties(zbuild_compatibility_version PROPERTIES
        PASS_REGULAR_EXPRESSION "ZPBuild 1\\.0\\.0"
    )
    add_test(NAME zpm_version COMMAND zpm version)
    set_tests_properties(zpm_version PROPERTIES
        PASS_REGULAR_EXPRESSION "ZPM 1\\.0\\.0"
    )
    add_executable(zbuild_unit_tests
        tests/zbuild/unit_tests.cpp
    )
    target_link_libraries(zbuild_unit_tests PRIVATE zbuild_engine)
    add_test(NAME zbuild_unit COMMAND zbuild_unit_tests)
    add_test(
        NAME zpm_local_package_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZPM=$<TARGET_FILE:zpm>"
            "-DZBUILD=$<TARGET_FILE:zpbuild>"
            "-DZP=$<TARGET_FILE:zp>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zpm-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/zpm/run_local_package.cmake"
    )
    add_test(
        NAME resource_copy_incremental
        COMMAND ${CMAKE_COMMAND}
            "-DCOPY_HELPER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyDirectoryIfDifferent.cmake"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/resource-copy-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_copy_directory_if_different.cmake"
    )
    add_test(
        NAME zbuild_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZBUILD=$<TARGET_FILE:zbuild>"
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zbuild-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/zbuild/run_integration.cmake"
    )
    set_tests_properties(zbuild_integration PROPERTIES DEPENDS zbuild_unit)
    add_test(
        NAME zbuild_templates
        COMMAND ${CMAKE_COMMAND}
            "-DZBUILD=$<TARGET_FILE:zbuild>"
            "-DZP=$<TARGET_FILE:zp>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zbuild-template-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/zbuild/run_template_tests.cmake"
    )
    set_tests_properties(zbuild_templates PROPERTIES DEPENDS zbuild_unit)
    add_test(
        NAME zbuild_backend_runtime
        COMMAND ${CMAKE_COMMAND}
            "-DZBUILD=$<TARGET_FILE:zbuild>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zbuild-backend-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/zbuild/run_backend_integration.cmake"
    )
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME zbuild_audio_integration
            COMMAND ${CMAKE_COMMAND}
                "-DZBUILD=$<TARGET_FILE:zbuild>"
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zbuild-audio-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/zbuild/run_audio_integration.cmake"
        )
        set_tests_properties(zbuild_audio_integration PROPERTIES TIMEOUT 180)
    endif()

    add_executable(zp_standard_library_unit_tests
        tests/stdlib/standard_library_unit_tests.cpp
        runtime/StandardLibrary.cpp
    )
    target_include_directories(zp_standard_library_unit_tests PRIVATE
        runtime
    )
    add_test(
        NAME standard_library_unicode
        COMMAND zp_standard_library_unit_tests
    )
    add_executable(zaudio_unit_tests tests/audio/audio_tests.cpp)
    target_include_directories(zaudio_unit_tests PRIVATE runtime)
    target_compile_definitions(zaudio_unit_tests PRIVATE
        "ZAUDIO_TEST_ASSET_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/test_program/audio/assets\""
    )
    target_link_libraries(zaudio_unit_tests PRIVATE zaudio_engine)
    add_test(NAME zaudio_api_headless COMMAND zaudio_unit_tests)
    add_test(
        NAME zaudio_interpreter_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/audio-interpreter-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio/run_interpreter.cmake"
    )
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME zaudio_native_integration
            COMMAND ${CMAKE_COMMAND}
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/audio-native-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/audio/run_native.cmake"
        )
        set_tests_properties(zaudio_native_integration PROPERTIES TIMEOUT 120)
    endif()
    add_executable(zphysics_unit_tests tests/physics/physics_tests.cpp)
    target_include_directories(zphysics_unit_tests PRIVATE runtime/physics)
    target_link_libraries(zphysics_unit_tests PRIVATE zphysics_engine)
    add_test(NAME zphysics_api_headless COMMAND zphysics_unit_tests)
    add_executable(zphysics_benchmark tests/physics/physics_benchmark.cpp)
    target_include_directories(zphysics_benchmark PRIVATE runtime/physics)
    target_link_libraries(zphysics_benchmark PRIVATE zphysics_engine)
    add_test(
        NAME zphysics_interpreter_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/physics-interpreter-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/physics/run_interpreter.cmake"
    )
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME zphysics_native_integration
            COMMAND ${CMAKE_COMMAND}
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/physics-native-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/physics/run_native.cmake"
        )
        set_tests_properties(zphysics_native_integration PROPERTIES TIMEOUT 120)
        add_test(
            NAME zbuild_physics_integration
            COMMAND ${CMAKE_COMMAND}
                "-DZBUILD=$<TARGET_FILE:zbuild>"
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zbuild-physics-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/zbuild/run_physics_integration.cmake"
        )
        set_tests_properties(zbuild_physics_integration PROPERTIES TIMEOUT 180)
    endif()
    add_executable(zp_json_unit_tests
        tests/stdlib/json_tests.cpp
        runtime/Json.cpp
    )
    target_include_directories(zp_json_unit_tests PRIVATE runtime)
    add_test(NAME json_standard_library COMMAND zp_json_unit_tests)
    add_executable(zp_sqlite_unit_tests
        tests/stdlib/sqlite_tests.cpp
        runtime/SQLite.cpp
        runtime/Json.cpp
    )
    target_include_directories(zp_sqlite_unit_tests PRIVATE runtime)
    if(CMAKE_DL_LIBS)
        target_link_libraries(zp_sqlite_unit_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_test(
        NAME sqlite_runtime
        COMMAND ${CMAKE_COMMAND}
            "-DSQLITE_TEST=$<TARGET_FILE:zp_sqlite_unit_tests>"
            "-DSQLITE_RUNTIME_DIR=$<TARGET_FILE_DIR:zp>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/run_sqlite_tests.cmake"
    )
    add_test(
        NAME sqlite_zplus_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/sqlite-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/run_sqlite_integration.cmake"
    )
    add_executable(zp_http_server_tests
        tests/http/http_server_tests.cpp
        runtime/Base64.cpp
        runtime/Http.cpp
        runtime/Json.cpp
    )
    target_include_directories(zp_http_server_tests PRIVATE runtime)
    target_link_libraries(zp_http_server_tests PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(zp_http_server_tests PRIVATE ws2_32)
    endif()
    add_test(NAME http_server_runtime COMMAND zp_http_server_tests)
    add_test(
        NAME http_zplus_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/http-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/http/run_http_smoke.cmake"
    )
    if(WIN32)
        add_test(
            NAME http_live_program_integration
            COMMAND ${CMAKE_COMMAND}
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/http-live-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/http/run_live_program_tests.cmake"
        )
        set_tests_properties(http_live_program_integration PROPERTIES TIMEOUT 150)
    endif()
    add_test(
        NAME json_interpreter_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/json-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/run_json_tests.cmake"
    )
    add_executable(zp_web_runtime_tests
        tests/web/web_runtime_tests.cpp
        runtime/Base64.cpp
        runtime/Web.cpp
        runtime/WebMultipart.cpp
        runtime/WebSessions.cpp
        runtime/WebTemplate.cpp
        runtime/WebValidation.cpp
        runtime/WebMiddleware.cpp
        runtime/Http.cpp
        runtime/Json.cpp
    )
    target_include_directories(zp_web_runtime_tests PRIVATE runtime)
    target_link_libraries(zp_web_runtime_tests PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(zp_web_runtime_tests PRIVATE ws2_32 bcrypt)
    endif()
    add_test(NAME web_runtime_utilities COMMAND zp_web_runtime_tests)
    add_test(
        NAME zweb_interpreter_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zweb-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/web/run_zweb_tests.cmake"
    )

    add_executable(zp_native_ir_tests
        tests/native/unsupported_ir.cpp
    )
    target_include_directories(zp_native_ir_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        compiler
    )
    target_link_libraries(zp_native_ir_tests PRIVATE zp_native_backend)
    add_test(
        NAME native_unsupported_ir
        COMMAND zp_native_ir_tests ${CMAKE_CURRENT_BINARY_DIR}/format-test.elf
    )
    add_executable(zp_native_abi_metadata_tests
        tests/native/abi_metadata.cpp
    )
    target_include_directories(zp_native_abi_metadata_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        compiler
    )
    target_link_libraries(zp_native_abi_metadata_tests PRIVATE zp_native_backend)
    add_test(
        NAME native_abi_metadata
        COMMAND zp_native_abi_metadata_tests
            ${CMAKE_CURRENT_BINARY_DIR}/native-abi-unwind.exe
    )
    add_test(
        NAME native_executable_integration
        COMMAND ${CMAKE_COMMAND}
            -DZP=$<TARGET_FILE:zp>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/native-test-output
            -DHOST_WINDOWS=$<BOOL:${WIN32}>
            ${ZPLUS_GRAPHICS_RUNTIME_ARGUMENT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/run_native_tests.cmake
    )
    add_test(
        NAME module_system_integration
        COMMAND ${CMAKE_COMMAND}
            -DZP=$<TARGET_FILE:zp>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/module-test-output
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules/run_module_tests.cmake
    )
    add_test(
        NAME runtime_arrays_and_diagnostics
        COMMAND ${CMAKE_COMMAND}
            -DZP=$<TARGET_FILE:zp>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/runtime-test-output
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/run_runtime_tests.cmake
    )
    add_test(
        NAME runtime_error_handling
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/error-handling-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/run_error_handling.cmake"
    )
    add_test(
        NAME feature_tour_integration
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/feature-tour-test-output"
            "-DHOST_WINDOWS=$<BOOL:${WIN32}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_feature_tour.cmake"
    )
    add_test(
        NAME parser_semantics_parity
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/parser-semantics-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/parser/run_semantics.cmake"
    )
    add_test(
        NAME parser_brace_syntax
        COMMAND ${CMAKE_COMMAND}
            "-DZP=$<TARGET_FILE:zp>"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/parser-brace-test-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/parser/run_brace_syntax.cmake"
    )
    add_test(
        NAME standard_library_integration
        COMMAND ${CMAKE_COMMAND}
            -DZP=$<TARGET_FILE:zp>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/stdlib-test-output
            -DHOST_WINDOWS=$<BOOL:${WIN32}>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/run_stdlib_tests.cmake
    )
    add_test(
        NAME standard_library_module_imports
        COMMAND $<TARGET_FILE:zp> run
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stdlib/module_imports.zp
    )
    set_tests_properties(standard_library_module_imports PROPERTIES
        PASS_REGULAR_EXPRESSION "all standard-library modules imported"
    )
    if(WIN32)
        add_test(
            NAME installer_platform_api
            COMMAND ${CMAKE_COMMAND}
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installer-platform-api-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installer/run_platform_api.cmake"
        )
        set_tests_properties(installer_platform_api PROPERTIES TIMEOUT 180)
        add_test(
            NAME zplus_installer_workflow
            COMMAND ${CMAKE_COMMAND}
                "-DZP=$<TARGET_FILE:zp>"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/zplus-installer-workflow-test-output"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/installer/windows-Z+/tests/run_workflow.cmake"
        )
        set_tests_properties(zplus_installer_workflow PROPERTIES TIMEOUT 180)
        add_test(
            NAME zplus_installer_ui_actions
            COMMAND $<TARGET_FILE:zp> run
                ${CMAKE_CURRENT_SOURCE_DIR}/installer/windows-Z+/tests/ui_actions.zp
        )
        set_tests_properties(zplus_installer_ui_actions PROPERTIES
            PASS_REGULAR_EXPRESSION "installer-ui-actions-passed"
        )
    endif()
    add_executable(zgraphics_core_tests
        tests/graphics/core_tests.cpp
    )
    target_link_libraries(zgraphics_core_tests PRIVATE zplus_graphics)
    add_test(NAME zgraphics_core COMMAND zgraphics_core_tests)
    add_test(
        NAME zgraphics_module_check
        COMMAND $<TARGET_FILE:zp> check
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphics/interpreter_smoke.zp
    )
    if(WIN32)
        add_test(
            NAME zgraphics_interpreter
            COMMAND $<TARGET_FILE:zp> run
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphics/interpreter_smoke.zp
        )
        set_tests_properties(zgraphics_interpreter PROPERTIES
            PASS_REGULAR_EXPRESSION "ZGraphics interpreter integration passed"
        )
        add_test(
            NAME zgraphics_interpreter_rendering
            COMMAND $<TARGET_FILE:zp> run
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/graphics/rendering_smoke.zp
        )
        set_tests_properties(zgraphics_interpreter_rendering PROPERTIES
            PASS_REGULAR_EXPRESSION "ZGraphics buffered rendering smoke passed"
        )
    endif()
endif()
