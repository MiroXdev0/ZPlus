
#include "lexer/Lexer.hpp"
#include "parser/Parser.hpp"
#include "ModuleLoader.hpp"
#include "semantic/Analyzer.hpp"
#include "ir/IR.hpp"
#include "ir/IRVerifier.hpp"
#include "backend/native/NativeCodeGenerator.hpp"
#include "../runtime/Runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <exception>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif


// ============================================================
// Z+ Compiler
// ============================================================

namespace zplus {

#ifndef ZPLUS_VERSION
#define ZPLUS_VERSION "1.4.0"
#endif

constexpr const char* VERSION = ZPLUS_VERSION;
constexpr const char* COMPILER_NAME = "Z+ Compiler";
constexpr const char* EXECUTABLE_NAME = "zp";

}


// ============================================================
// Helpers
// ============================================================

namespace {

bool diagnosticsJson = false;

std::filesystem::path pathFromUtf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(encoded);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[(character >> 4) & 0x0F];
                    escaped += hex[character & 0x0F];
                } else {
                    escaped += static_cast<char>(character);
                }
        }
    }
    return escaped;
}

std::string expandTabs(const std::string& line) {
    std::string expanded;
    std::size_t column = 0;
    for (const char character : line) {
        if (character == '\t') {
            const std::size_t spaces = 4 - (column % 4);
            expanded.append(spaces, ' ');
            column += spaces;
        } else {
            expanded += character;
            ++column;
        }
    }
    return expanded;
}

bool readSourceLine(
    const std::string& sourceFile,
    int lineNumber,
    std::string& line
) {
    if (sourceFile.empty() || lineNumber < 1) {
        return false;
    }
    std::ifstream source(pathFromUtf8(sourceFile));
    if (!source) {
        return false;
    }
    for (int currentLine = 1; currentLine <= lineNumber; ++currentLine) {
        if (!std::getline(source, line)) {
            return false;
        }
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

std::string diagnosticCategory(
    zplus::DiagnosticCategory category
) {
    switch (category) {
        case zplus::DiagnosticCategory::Lexical: return "Lexical error";
        case zplus::DiagnosticCategory::Syntax: return "Syntax error";
        case zplus::DiagnosticCategory::Module: return "Module/import error";
        case zplus::DiagnosticCategory::Type: return "Type error";
        case zplus::DiagnosticCategory::Semantic: return "Semantic error";
        case zplus::DiagnosticCategory::IR: return "IR error";
        case zplus::DiagnosticCategory::Backend: return "Backend error";
        case zplus::DiagnosticCategory::Runtime: return "Runtime error";
        case zplus::DiagnosticCategory::General: return "Compiler error";
    }
    return "Compiler error";
}

zplus::DiagnosticCategory diagnosticCategoryForCode(const std::string& code) {
    if (code == "E1000") return zplus::DiagnosticCategory::Lexical;
    if (code == "E1001") return zplus::DiagnosticCategory::Syntax;
    if (code == "E2001") return zplus::DiagnosticCategory::Semantic;
    if (code == "E3001") return zplus::DiagnosticCategory::Module;
    if (code == "E4001") return zplus::DiagnosticCategory::IR;
    if (code == "E5001") return zplus::DiagnosticCategory::Backend;
    if (code == "E6001") return zplus::DiagnosticCategory::Runtime;
    return zplus::DiagnosticCategory::General;
}

std::string diagnosticSuggestion(
    const std::string& code,
    const std::string& message
) {
    if (code != "E1001") return {};
    if (message.find("expected ':'") != std::string::npos) {
        return "Add ':' at the indicated location.";
    }
    if (message.find("expected ')'") != std::string::npos) {
        if (message.find("RightBracket") != std::string::npos ||
            message.find("found ']'") != std::string::npos) {
            return "Replace ']' with ')'.";
        }
        return "Add ')' to close the expression.";
    }
    if (message.find("expected ']'") != std::string::npos) {
        return "Add ']' to close the index or array.";
    }
    if (message.find("expected '}'") != std::string::npos) {
        return "Add '}' to close the block.";
    }
    if (message.find("expected 'end'") != std::string::npos) {
        return "Add 'end' to close the block.";
    }
    if (message.find("expected 'stop'") != std::string::npos) {
        return "Add 'stop' to close the start block.";
    }
    return {};
}

// ============================================================
// Console
// ============================================================

void setupConsole() {

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

}


// ============================================================
// Usage
// ============================================================

void usage() {

    std::cout
        << "Z+ Compiler " << zplus::VERSION << "\n\n"

        << "Usage:\n"
        << "  zp <command> <file>\n\n"

        << "Commands:\n"
        << "  build    Compile to a native executable\n"
        << "  run      Compile and run a Z+ source file\n"
        << "  check    Check a Z+ source file\n"
        << "  dependencies  List resolved source and module files as JSON\n"
        << "  version  Show compiler version\n"
        << "  help     Show this help message\n\n"

        << "Options:\n"
        << "  -o <file>    Set native executable output path (build only)\n"
        << "  --windows-gui  Use the Windows GUI subsystem (Windows builds only)\n"
        << "  --source-stdin  Read check source from standard input\n"
        << "  -h, --help   Show this help message\n\n"

        << "Examples:\n"
        << "  zp run hello.zp\n"
        << "  zp run hello.zp -- input.txt --verbose\n"
        << "  zp check hello.zp\n"
        << "  zp build hello.zp\n";
}


// ============================================================
// Error reporting
// ============================================================

void printError(
    const std::string& message,
    int line = 0,
    int column = 0,
    const std::string& sourceFile = {},
    const std::string& code = "E0001",
    zplus::DiagnosticCategory diagnosticKind = zplus::DiagnosticCategory::General,
    const std::vector<zplus::RelatedDiagnosticInformation>& relatedInformation = {}
) {

    if (diagnosticKind == zplus::DiagnosticCategory::General) {
        diagnosticKind = diagnosticCategoryForCode(code);
    }
    const std::string category = diagnosticCategory(diagnosticKind);
    const std::string suggestion = diagnosticSuggestion(code, message);
    std::string sourceLine;
    if (!sourceFile.empty() && line > 0) {
        readSourceLine(sourceFile, line, sourceLine);
    }

    if (diagnosticsJson) {
        std::cerr << "{\"severity\":\"error\",\"code\":\""
                  << jsonEscape(code) << "\",\"message\":\""
                  << jsonEscape(message) << "\",\"category\":\""
                  << jsonEscape(category) << "\",\"file\":\""
                  << jsonEscape(sourceFile) << "\",\"line\":" << line
                  << ",\"column\":" << column << ",\"sourceLine\":\""
                  << jsonEscape(sourceLine) << "\",\"suggestion\":\""
                  << jsonEscape(suggestion) << "\",\"relatedInformation\":[";
        for (std::size_t index = 0; index < relatedInformation.size(); ++index) {
            if (index != 0) std::cerr << ',';
            const auto& related = relatedInformation[index];
            std::cerr << "{\"message\":\"" << jsonEscape(related.message)
                      << "\",\"file\":\"" << jsonEscape(related.sourceFile)
                      << "\",\"line\":" << related.line
                      << ",\"column\":" << related.column << '}';
        }
        std::cerr << "]}\n";
        return;
    }

    std::cerr << "error[" << code << "] (" << category << "): "
              << message << '\n';

    if (!sourceFile.empty()) {
        std::cerr << "  --> " << sourceFile;
        if (line > 0) {
            std::cerr << ':' << line;
            if (column > 0) {
                std::cerr << ':' << column;
            }
        }
        std::cerr << '\n';

        if (line > 0 && !sourceLine.empty()) {
            const std::string expandedLine = expandTabs(sourceLine);
            std::size_t caretColumn = 0;
            std::size_t prefixLength = column > 1
                ? static_cast<std::size_t>(column - 1)
                : 0;
            if (prefixLength > sourceLine.size()) {
                prefixLength = sourceLine.size();
            }
            for (std::size_t index = 0; index < prefixLength; ++index) {
                caretColumn += sourceLine[index] == '\t'
                    ? 4 - (caretColumn % 4)
                    : 1;
            }
            const std::size_t lineWidth = std::to_string(line).size();
            std::cerr << std::string(lineWidth + 1, ' ') << "|\n"
                      << std::setw(static_cast<int>(lineWidth)) << line
                      << " | " << expandedLine << '\n'
                      << std::string(lineWidth + 1, ' ') << "| "
                      << std::string(caretColumn, ' ') << "^\n";
        }
    }
    if (!suggestion.empty()) {
        std::cerr << "  help: " << suggestion << '\n';
    }
}

void printWarning(
    const std::string& message,
    int line = 0,
    int column = 0,
    const std::string& sourceFile = {}
) {
    if (diagnosticsJson) {
        std::cerr << "{\"severity\":\"warning\",\"message\":\""
                  << jsonEscape(message) << "\",\"file\":\""
                  << jsonEscape(sourceFile) << "\",\"line\":" << line
                  << ",\"column\":" << column << "}\n";
        return;
    }

    std::cerr << "warning: " << message;
    if (!sourceFile.empty()) std::cerr << " in '" << sourceFile << "'";
    if (line > 0) std::cerr << " at " << line << ':' << column;
    std::cerr << '\n';
}


// ============================================================
// Validate input
// ============================================================

bool validateInputFile(
    const std::string& filename
) {

    if (filename.empty()) {
        printError(
            "input filename cannot be empty"
        );

        return false;
    }

    const std::filesystem::path path = pathFromUtf8(filename);

    if (!std::filesystem::exists(path)) {
        printError(
            "file '" + filename + "' does not exist"
        );

        return false;
    }

    if (!std::filesystem::is_regular_file(path)) {
        printError(
            "'" + filename + "' is not a regular file"
        );

        return false;
    }

    return true;
}


// ============================================================
// Compilation pipeline
// ============================================================

bool compile(
    const zplus::ast::Program& program,
    zplus::ir::Module& module
) {
    // ========================================================
    // Semantic analysis
    // ========================================================

    zplus::semantic::Analyzer analyzer;

    const bool analyzed = analyzer.analyze(program);
    for (const auto& warning : analyzer.warnings()) {
        printWarning(
            warning.message,
            static_cast<int>(warning.line),
            static_cast<int>(warning.column),
            warning.sourceFile
        );
    }

    if (!analyzed) {
        for (const auto& error : analyzer.errors()) {

            printError(
                error.message,
                static_cast<int>(error.line),
                static_cast<int>(error.column),
                error.sourceFile,
                error.code,
                error.category,
                error.relatedInformation
            );
        }

        return false;
    }


    // ========================================================
    // IR generation
    // ========================================================

    try {

        zplus::ir::IRBuilder builder;

        module = builder.build(program, analyzer.builtinRegistry());

    }
    catch (const std::exception& error) {

        printError(
            std::string("IR generation failed: ") +
            error.what()
        );

        return false;
    }

    const auto irDiagnostics = zplus::ir::verifyModule(module);
    if (!irDiagnostics.empty()) {
        for (const auto& diagnostic : irDiagnostics) {
            printError(
                diagnostic.message,
                static_cast<int>(diagnostic.line),
                static_cast<int>(diagnostic.column),
                diagnostic.sourceFile,
                diagnostic.code,
                diagnostic.category,
                diagnostic.relatedInformation
            );
        }
        return false;
    }

    return true;
}


} // namespace


// ============================================================
// Main
// ============================================================

int runCli(
    int argc,
    char** argv
) {

    setupConsole();


    // ========================================================
    // No arguments
    // ========================================================

    if (argc < 2) {

        usage();

        return 1;
    }


    const std::string command = argv[1];


    // ========================================================
    // Help
    // ========================================================

    if (
        command == "help" ||
        command == "--help" ||
        command == "-h"
    ) {

        usage();

        return 0;
    }


    // ========================================================
    // Version
    // ========================================================

    if (
        command == "version" ||
        command == "--version" ||
        command == "-v"
    ) {

        std::cout
            << zplus::COMPILER_NAME
            << ' '
            << zplus::VERSION
            << '\n';

        return 0;
    }


    // ========================================================
    // Validate command
    // ========================================================

    if (
        command != "build" &&
        command != "run" &&
        command != "check" &&
        command != "dependencies" &&
        command != "deps"
    ) {

        printError(
            "unknown command '" + command + "'"
        );

        std::cerr << '\n';

        usage();

        return 1;
    }


    // ========================================================
    // ========================================================
    // Input file
    // ========================================================

    if (argc < 3) {

        printError(
            "missing input file"
        );

        std::cerr << '\n';

        usage();

        return 1;
    }


    const std::string filename = argv[2];
    std::filesystem::path outputPath;
    bool outputPathSet = false;
    bool windowsGui = false;
    bool sourceFromStdin = false;
    std::vector<std::string> programArguments;

    // ========================================================
    // Options
    // ========================================================

    for (int i = 3; i < argc; ++i) {

        const std::string argument = argv[i];

        if (command == "run" && argument == "--") {
            for (++i; i < argc; ++i) {
                programArguments.emplace_back(argv[i]);
            }
            break;
        }

        if (
            argument == "--help" ||
            argument == "-h"
        ) {

            usage();

            return 0;
        }

        if (command == "build" && (argument == "-o" || argument == "--output")) {
            if (outputPathSet || i + 1 >= argc || std::string(argv[i + 1]).empty()) {
                printError("option '" + argument + "' requires exactly one output path");
                return 1;
            }
            outputPath = pathFromUtf8(argv[++i]);
            outputPathSet = true;
            continue;
        }

        if (argument == "--windows-gui") {
            if (command != "build") {
                printError("option '--windows-gui' is valid only with the build command");
                return 1;
            }
            windowsGui = true;
            continue;
        }

        if (argument == "--source-stdin") {
            if (command != "check") {
                printError("option '--source-stdin' is valid only with the check command");
                return 1;
            }
            sourceFromStdin = true;
            continue;
        }

        if (argument.starts_with("--diagnostics=")) {
            const std::string format = argument.substr(std::string("--diagnostics=").size());
            if (format != "json") {
                printError("unsupported diagnostic format '" + format + "'");
                return 1;
            }
            diagnosticsJson = true;
            continue;
        }

        printError(
            "unknown option '" + argument + "'"
        );

        return 1;
    }

    std::string sourceText;
    if (sourceFromStdin) {
        std::ostringstream input;
        input << std::cin.rdbuf();
        if (std::cin.bad()) {
            printError("failed to read source from standard input");
            return 1;
        }
        sourceText = input.str();
    } else if (!validateInputFile(filename)) {
        return 1;
    }


    // ========================================================
    // Load module graph
    // ========================================================

    zplus::ModuleLoader moduleLoader;
    auto loaded = moduleLoader.load(
        pathFromUtf8(filename),
        sourceFromStdin ? &sourceText : nullptr);
    for (const auto& warning : loaded.warnings) {
        printWarning(
            warning.message,
            warning.line,
            warning.column,
            warning.sourceFile
        );
    }
    for (const auto& diagnostic : loaded.diagnostics) {
        printError(
            diagnostic.message,
            diagnostic.line,
            diagnostic.column,
            diagnostic.sourceFile,
            diagnostic.code
        );
    }
    if (!loaded.diagnostics.empty() || !loaded.program) {
        return 1;
    }

    if (command == "dependencies" || command == "deps") {
        std::sort(loaded.sourceFiles.begin(), loaded.sourceFiles.end());
        std::cout << '[';
        for (std::size_t i = 0; i < loaded.sourceFiles.size(); ++i) {
            if (i != 0) std::cout << ',';
            std::cout << '"' << jsonEscape(pathToUtf8(loaded.sourceFiles[i])) << '"';
        }
        std::cout << "]\n";
        return 0;
    }


    // ========================================================
    // Compile
    // ========================================================

    zplus::ir::Module module;

    if (!compile(*loaded.program, module)) {
        return 1;
    }

    if (command == "build") {
        if (!outputPathSet) {
#ifdef _WIN32
            outputPath = pathFromUtf8(filename).replace_extension(".exe");
#else
            outputPath = pathFromUtf8(filename).replace_extension(".out");
#endif
        }

        std::error_code inputPathError;
        std::error_code outputPathError;
        const auto absoluteInput = std::filesystem::weakly_canonical(
            pathFromUtf8(filename), inputPathError);
        const auto absoluteOutput = std::filesystem::weakly_canonical(outputPath, outputPathError);
        if (!inputPathError && !outputPathError && absoluteInput == absoluteOutput) {
            printError("native output path must not overwrite the source file");
            return 1;
        }

        try {
            zplus::backend::native::NativeCodeGenerator generator;
            generator.writeExecutable(module, outputPath, windowsGui);
        }
        catch (const std::exception& error) {
            printError(
                std::string("native compilation failed: ") + error.what(),
                0,
                0,
                {},
                "E5001"
            );
            return 1;
        }

        std::cout << "built: " << pathToUtf8(outputPath) << '\n';
        return 0;
    }

    // ========================================================
    // Check
    // ========================================================

    if (command == "check") {

        std::cout
            << "ok: "
            << filename
            << '\n';

        return 0;
    }

    // ========================================================
    // Run
    // ========================================================

    if (command == "run") {

        try {

            zplus::runtime::Runtime runtime;

            runtime.execute(module, programArguments);

        }
        catch (const std::exception& error) {

            printError(
                std::string("runtime error: ") +
                error.what(),
                0,
                0,
                {},
                "E6001"
            );

            return 1;
        }

        return 0;
    }

    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wideArguments) {
    std::vector<std::string> converted;
    std::vector<char*> arguments;
    converted.reserve(static_cast<std::size_t>(argc));
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const int length = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[i], -1, nullptr, 0,
            nullptr, nullptr);
        if (length <= 0) {
            std::cerr << "command-line argument is not valid Unicode\n";
            return 1;
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[i], -1,
                value.data(), length, nullptr, nullptr) <= 0) {
            std::cerr << "failed converting a command-line argument\n";
            return 1;
        }
        value.resize(static_cast<std::size_t>(length - 1));
        converted.push_back(std::move(value));
    }
    for (std::string& value : converted) arguments.push_back(value.data());
    return runCli(argc, arguments.data());
}
#else
int main(int argc, char** argv) {
    return runCli(argc, argv);
}
#endif
