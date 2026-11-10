#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zplus::stdlib {

double mathUnary(const std::string& operation, double value);
double mathBinary(const std::string& operation, double left, double right);
double mathConstant(const std::string& name);

std::string stringUnary(const std::string& operation, const std::string& value);
std::string stringBinary(
    const std::string& operation,
    const std::string& value,
    const std::string& other
);
std::string stringReplace(
    const std::string& value,
    const std::string& search,
    const std::string& replacement
);
std::string stringJoin(
    const std::vector<std::string>& values,
    const std::string& separator
);
std::int64_t stringLength(const std::string& value);
std::string substring(
    const std::string& value,
    std::int64_t start,
    std::int64_t length
);
std::string repeat(const std::string& value, std::int64_t count);
std::vector<std::string> split(
    const std::string& value,
    const std::string& delimiter
);

std::string pathUnary(const std::string& operation, const std::string& path);
std::string pathJoin(const std::string& left, const std::string& right);

std::string fileRead(const std::string& path);
void fileWrite(const std::string& path, const std::string& contents, bool append);
bool fileExists(const std::string& path);
bool fileDelete(const std::string& path);
bool fileCreateDirectory(const std::string& path);
std::vector<std::string> fileListDirectory(const std::string& path);
std::int64_t fileSize(const std::string& path);
bool fileIsDirectory(const std::string& path);

std::string environmentGet(const std::string& name);
void environmentSet(const std::string& name, const std::string& value);
bool environmentExists(const std::string& name);
std::string homeDirectory();
std::string currentDirectory();

double timeNow();
std::string timeFormat(double timestamp, const std::string& format);
double timeParse(const std::string& value, const std::string& format);
double timePart(const std::string& part, double timestamp);
void timeSleep(std::int64_t milliseconds);

std::int64_t randomInteger(std::int64_t minimum, std::int64_t maximum);
double randomNumber(double minimum, double maximum);
bool randomBoolean();
void randomSeed(std::uint64_t seed);

void setProcessArguments(std::vector<std::string> arguments);
const std::vector<std::string>& processArguments();

void logWrite(const std::string& level, const std::string& message);
void logSetLevel(const std::string& level);
void logSetOutput(const std::string& path);

} // namespace zplus::stdlib
