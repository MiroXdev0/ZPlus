#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace zbuild {

struct TemplateInfo {
    std::string name;
    std::string description;
    std::string version;
    std::string requiredZPlusVersion;
    std::string defaultTargetType;
    std::vector<std::string> platforms;
    std::vector<std::string> requiredModules;
    std::vector<std::string> aliases;
    std::vector<std::string> files;
    std::vector<std::string> directories;
    std::vector<std::string> configuration;
    bool available = true;
    std::string unavailableReason;
    std::filesystem::path root;
    bool builtIn = false;
};

class TemplateRegistry {
public:
    explicit TemplateRegistry(std::filesystem::path executablePath = {});

    std::vector<TemplateInfo> list() const;
    TemplateInfo find(const std::string& name) const;
    void printList() const;
    void printInfo(const std::string& name) const;
    void initialize(
        const std::string& templateName,
        const std::filesystem::path& destination,
        const std::string& projectName,
        const std::string& projectVersion = "0.1.0",
        const std::string& author = {}
    ) const;

private:
    std::filesystem::path executablePath_;
    std::filesystem::path builtInDirectory_;
    std::filesystem::path customDirectory_;
};

} // namespace zbuild
