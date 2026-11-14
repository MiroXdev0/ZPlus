#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace zplus {

enum class DiagnosticCategory {
    General,
    Lexical,
    Syntax,
    Module,
    Type,
    Semantic,
    IR,
    Backend,
    Runtime
};

struct RelatedDiagnosticInformation {
    std::string message;
    std::string sourceFile;
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Diagnostic {
    std::string code = "E2001";
    DiagnosticCategory category = DiagnosticCategory::Semantic;
    std::string message;
    std::string sourceFile;
    std::size_t line = 0;
    std::size_t column = 0;
    std::vector<RelatedDiagnosticInformation> relatedInformation;

    Diagnostic() = default;

    Diagnostic(
        std::string diagnosticMessage,
        std::string file,
        std::size_t diagnosticLine,
        std::size_t diagnosticColumn,
        std::string diagnosticCode = "E2001",
        DiagnosticCategory diagnosticCategory = DiagnosticCategory::Semantic
    )
        : code(std::move(diagnosticCode)),
          category(diagnosticCategory),
          message(std::move(diagnosticMessage)),
          sourceFile(std::move(file)),
          line(diagnosticLine),
          column(diagnosticColumn) {
    }
};

} // namespace zplus
