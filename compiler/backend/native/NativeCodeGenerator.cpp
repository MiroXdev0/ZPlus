#include "NativeCodeGenerator.hpp"

#include "ELFWriter.hpp"
#include "PEWriter.hpp"
#include "X64CodeGenerator.hpp"

#include <fstream>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace zplus::backend::native {
namespace {

#if defined(_WIN32)
std::filesystem::path compilerDirectory() {
    std::vector<wchar_t> path(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            throw std::runtime_error("cannot locate the running Z+ compiler executable");
        }
        if (length < path.size()) {
            return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
        }
        if (path.size() > 32768) {
            throw std::runtime_error("Z+ compiler executable path is too long");
        }
        path.resize(path.size() * 2);
    }
}
#elif defined(__linux__)
std::filesystem::path compilerDirectory() {
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    if (error) {
        throw std::runtime_error(
            "cannot locate the running Z+ compiler executable: " + error.message());
    }
    return executable.parent_path();
}
#endif

} // namespace

std::vector<std::uint8_t> NativeCodeGenerator::generate(
    const zplus::ir::Module& module
) const {
#if !defined(_M_X64) && !defined(__x86_64__)
    (void)module;
    throw std::runtime_error("native code generation is supported only on x86-64 hosts");
#elif defined(_WIN32)
    return writePE(generateX64(module, true));
#elif defined(__linux__)
    return writeELF(generateX64(module, false));
#else
    (void)module;
    throw std::runtime_error("native code generation is supported only on Windows and Linux");
#endif
}

void NativeCodeGenerator::writeExecutable(
    const zplus::ir::Module& module,
    const std::filesystem::path& output,
    bool windowsGui
) const {
    if (output.empty()) {
        throw std::runtime_error("native output path cannot be empty");
    }

#if defined(_WIN32)
#if !defined(_M_X64) && !defined(__x86_64__)
    (void)module;
    (void)windowsGui;
    throw std::runtime_error("native code generation is supported only on x86-64 hosts");
#endif
    const NativeImage image = generateX64(module, true);
    const bool needsNativeRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "zgraphics_runtime.dll"; });
    const bool needsSQLiteRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) {
            return fixup.library == "zgraphics_runtime.dll" &&
                fixup.name.rfind("zpg_sqlite_", 0) == 0;
        });
    const bool needsAudioRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "zaudio_runtime.dll"; });
    const bool needsPhysicsRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "zphysics_runtime.dll"; });
    const bool needsInputRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "zplus_input_runtime.dll"; });
    const auto bytes = writePE(image, windowsGui);
#elif defined(__linux__)
    if (windowsGui) {
        throw std::runtime_error("the Windows GUI subsystem is available only for Windows executables");
    }
    const NativeImage image = generateX64(module, false);
    const bool needsAudioRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "libzaudio_runtime.so"; });
    const bool needsPhysicsRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "libzphysics_runtime.so"; });
    const bool needsInputRuntime = std::any_of(
        image.importFixups.begin(), image.importFixups.end(),
        [](const auto& fixup) { return fixup.library == "libzplus_input_runtime.so"; });
    const auto bytes = writeELF(image);
#else
    if (windowsGui) {
        throw std::runtime_error("the Windows GUI subsystem is available only for Windows executables");
    }
    const auto bytes = generate(module);
#endif
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("native executable is too large to write");
    }

#if defined(_WIN32)
    if (needsNativeRuntime) {
        const auto runtime = compilerDirectory() / "zgraphics_runtime.dll";
        if (!std::filesystem::is_regular_file(runtime)) {
            throw std::runtime_error(
                "required native runtime 'zgraphics_runtime.dll' was not found "
                "beside the Z+ compiler at '" + runtime.string() + "'");
        }
        std::error_code directoryError;
        const auto outputDirectory = output.has_parent_path()
            ? output.parent_path()
            : std::filesystem::current_path();
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            throw std::runtime_error("cannot create native output directory '" +
                                     outputDirectory.string() + "': " + directoryError.message());
        }
        const auto deployedRuntime = outputDirectory / runtime.filename();
        std::error_code copyError;
        if (std::filesystem::weakly_canonical(runtime) !=
            std::filesystem::weakly_canonical(deployedRuntime)) {
            std::filesystem::copy_file(
                runtime, deployedRuntime, std::filesystem::copy_options::overwrite_existing,
                copyError);
            if (copyError) {
                throw std::runtime_error("cannot package native graphics runtime beside output: " +
                                         copyError.message());
            }
        }
    }
    if (needsSQLiteRuntime) {
        const auto sqliteRuntime = compilerDirectory() / "sqlite3.dll";
        if (!std::filesystem::is_regular_file(sqliteRuntime)) {
            throw std::runtime_error(
                "SQLite was imported but 'sqlite3.dll' was not found beside the Z+ compiler");
        }
        std::error_code directoryError;
        const auto outputDirectory = output.has_parent_path()
            ? output.parent_path()
            : std::filesystem::current_path();
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            throw std::runtime_error("cannot create native output directory '" +
                                     outputDirectory.string() + "': " + directoryError.message());
        }
        const auto deployedRuntime = outputDirectory / sqliteRuntime.filename();
        std::error_code copyError;
        if (std::filesystem::weakly_canonical(sqliteRuntime) !=
            std::filesystem::weakly_canonical(deployedRuntime)) {
            std::filesystem::copy_file(
                sqliteRuntime, deployedRuntime,
                std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError) {
                throw std::runtime_error("cannot package SQLite runtime beside output: " +
                                         copyError.message());
            }
        }
    }
#endif

#if defined(_WIN32) || defined(__linux__)
    if (needsAudioRuntime) {
#if defined(_WIN32)
        const char* runtimeName = "zaudio_runtime.dll";
#else
        const char* runtimeName = "libzaudio_runtime.so";
#endif
        const auto runtime = compilerDirectory() / runtimeName;
        if (!std::filesystem::is_regular_file(runtime)) {
            throw std::runtime_error(
                "audio was imported but required runtime '" + runtime.string() +
                "' was not found beside the Z+ compiler");
        }
        std::error_code directoryError;
        const auto outputDirectory = output.has_parent_path()
            ? output.parent_path()
            : std::filesystem::current_path();
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            throw std::runtime_error("cannot create native output directory '" +
                outputDirectory.string() + "': " + directoryError.message());
        }
        const auto deployedRuntime = outputDirectory / runtime.filename();
        std::error_code copyError;
        if (std::filesystem::weakly_canonical(runtime) !=
            std::filesystem::weakly_canonical(deployedRuntime)) {
            std::filesystem::copy_file(runtime, deployedRuntime,
                std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError) {
                throw std::runtime_error("cannot package audio runtime beside output: " +
                    copyError.message());
            }
        }
    }
    if (needsPhysicsRuntime) {
#if defined(_WIN32)
        const char* runtimeName = "zphysics_runtime.dll";
#else
        const char* runtimeName = "libzphysics_runtime.so";
#endif
        const auto runtime = compilerDirectory() / runtimeName;
        if (!std::filesystem::is_regular_file(runtime)) {
            throw std::runtime_error(
                "physics was imported but required runtime '" + runtime.string() +
                "' was not found beside the Z+ compiler");
        }
        std::error_code directoryError;
        const auto outputDirectory = output.has_parent_path()
            ? output.parent_path()
            : std::filesystem::current_path();
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            throw std::runtime_error("cannot create native output directory '" +
                outputDirectory.string() + "': " + directoryError.message());
        }
        const auto deployedRuntime = outputDirectory / runtime.filename();
        std::error_code copyError;
        if (std::filesystem::weakly_canonical(runtime) !=
            std::filesystem::weakly_canonical(deployedRuntime)) {
            std::filesystem::copy_file(runtime, deployedRuntime,
                std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError) {
                throw std::runtime_error("cannot package physics runtime beside output: " +
                    copyError.message());
            }
        }
    }
    if (needsInputRuntime) {
#if defined(_WIN32)
        const char* runtimeName = "zplus_input_runtime.dll";
#else
        const char* runtimeName = "libzplus_input_runtime.so";
#endif
        const auto runtime = compilerDirectory() / runtimeName;
        if (!std::filesystem::is_regular_file(runtime)) {
            throw std::runtime_error(
                "terminal input was used but the required runtime '" +
                runtime.string() + "' was not found beside the Z+ compiler");
        }
        std::error_code directoryError;
        const auto outputDirectory = output.has_parent_path()
            ? output.parent_path()
            : std::filesystem::current_path();
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            throw std::runtime_error("cannot create native output directory '" +
                outputDirectory.string() + "': " + directoryError.message());
        }
        const auto deployedRuntime = outputDirectory / runtime.filename();
        std::error_code copyError;
        if (std::filesystem::weakly_canonical(runtime) !=
            std::filesystem::weakly_canonical(deployedRuntime)) {
            std::filesystem::copy_file(runtime, deployedRuntime,
                std::filesystem::copy_options::overwrite_existing, copyError);
            if (copyError) {
                throw std::runtime_error("cannot package terminal input runtime beside output: " +
                    copyError.message());
            }
        }
    }
#endif

    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("cannot open native output file '" + output.string() + "'");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("failed writing native output file '" + output.string() + "'");
    }
    file.close();
    if (!file) {
        throw std::runtime_error("failed closing native output file '" + output.string() + "'");
    }

#if !defined(_WIN32)
    std::filesystem::permissions(
        output,
        std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write |
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace
    );
#endif
}

} // namespace zplus::backend::native