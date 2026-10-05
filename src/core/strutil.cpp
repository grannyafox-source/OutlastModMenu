#include "strutil.h"

#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace omm::str {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool IStartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && IEquals(s.substr(0, prefix.size()), prefix);
}

bool IEndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && IEquals(s.substr(s.size() - suffix.size()), suffix);
}

bool IContains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

std::vector<std::string> Split(const std::string& s, char sep, bool trimParts) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(trimParts ? Trim(cur) : cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trimParts ? Trim(cur) : cur);
    return out;
}

std::string Join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string Format(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (static_cast<size_t>(n) < sizeof(buf)) return std::string(buf, static_cast<size_t>(n));
    std::string big(static_cast<size_t>(n) + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(&big[0], big.size(), fmt, ap);
    va_end(ap);
    big.resize(static_cast<size_t>(n));
    return big;
}

std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0xFFFD;
        size_t extra = 0;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { ++i; out.push_back(0xFFFD); continue; }
        bool ok = true;  // a truncated or malformed sequence becomes U+FFFD
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= s.size()) { ok = false; break; }
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(0xFFFD); ++i; continue; }
        i += extra + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string Utf16ToUtf8(const char16_t* s, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string Utf16ToUtf8(const std::u16string& s) { return Utf16ToUtf8(s.data(), s.size()); }

std::wstring Utf8ToWide(const std::string& s) {
    std::u16string u = Utf8ToUtf16(s);
    std::wstring w;
    w.reserve(u.size());
    for (char16_t c : u) w.push_back(static_cast<wchar_t>(c));
    return w;
}

std::string WideToUtf8(const std::wstring& s) {
    std::u16string u;
    u.reserve(s.size());
    for (wchar_t c : s) u.push_back(static_cast<char16_t>(c));
    return Utf16ToUtf8(u);
}

bool ParseFloat(const std::string& s, float& out) {
    std::string t = Trim(s);
    if (!t.empty() && (t.back() == 'f' || t.back() == 'F')) t.pop_back();  // UE3 accepts "2.0f"
    if (t.empty()) return false;
    char* end = nullptr;
    errno = 0;
    double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
    out = static_cast<float>(v);
    return true;
}

bool ParseInt(const std::string& s, int& out) {
    std::string t = Trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long v = std::strtol(t.c_str(), &end, 10);
    if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
    out = static_cast<int>(v);
    return true;
}

bool ParseBool(const std::string& s, bool& out) {
    std::string t = ToLower(Trim(s));
    if (t == "true" || t == "1" || t == "yes" || t == "on") { out = true; return true; }
    if (t == "false" || t == "0" || t == "no" || t == "off") { out = false; return true; }
    return false;
}

std::string FloatToIni(float v) {
    std::string s = Format("%.6f", static_cast<double>(v));
    // Trim trailing zeros but keep one digit after the decimal point.
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        while (s.size() > dot + 2 && s.back() == '0') s.pop_back();
    }
    return s;
}

}  // namespace omm::str
