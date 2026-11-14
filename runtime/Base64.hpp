#pragma once

#include <string>
#include <string_view>

namespace zplus::encoding {

std::string base64Encode(std::string_view value);
std::string base64Decode(std::string_view value);
std::string base64UrlEncode(std::string_view value);

} // namespace zplus::encoding
