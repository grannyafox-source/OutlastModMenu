// String helpers shared by every module (UTF conversion, trimming, parsing).
#pragma once

#include "common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omm::str {

std::string Trim(const std::string& s);
std::string ToLower(std::string s);
bool IEquals(const std::string& a, const std::string& b);
bool IStartsWith(const std::string& s, const std::string& prefix);
bool IEndsWith(const std::string& s, const std::string& suffix);
bool IContains(const std::string& haystack, const std::string& needle);
std::vector<std::string> Split(const std::string& s, char sep, bool trimParts = true);
std::string Join(const std::vector<std::string>& parts, const std::string& sep);
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to);
std::string Format(const char* fmt, ...) OMM_PRINTF(1, 2);

// UTF-8 <-> UTF-16 conversion that does not depend on the C locale.
std::u16string Utf8ToUtf16(const std::string& s);
std::string Utf16ToUtf8(const std::u16string& s);
std::string Utf16ToUtf8(const char16_t* s, size_t len);
std::wstring Utf8ToWide(const std::string& s);   // wchar_t is UTF-16 on Windows
std::string WideToUtf8(const std::wstring& s);

bool ParseFloat(const std::string& s, float& out);
bool ParseInt(const std::string& s, int& out);
bool ParseBool(const std::string& s, bool& out);  // true/false/1/0/yes/no/on/off

// Formats a float the way UE3 config files expect (no trailing garbage).
std::string FloatToIni(float v);

}  // namespace omm::str
