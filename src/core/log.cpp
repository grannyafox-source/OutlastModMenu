#include "log.h"

#include "strutil.h"
#include "sync.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>

namespace omm::log {

namespace {
Mutex g_mutex;
FILE* g_file = nullptr;
std::string g_path;
std::deque<std::string> g_ring;
Level g_minLevel = Level::Info;
constexpr size_t kRingSize = 1000;

const char* LevelTag(Level l) {
    switch (l) {
        case Level::Debug: return "DBG";
        case Level::Info: return "INF";
        case Level::Warn: return "WRN";
        case Level::Error: return "ERR";
    }
    return "???";
}
}  // namespace

void Init(const std::string& path) {
    LockGuard lock(g_mutex);
    if (g_file) return;
    g_path = path;
#if OMM_WINDOWS
    g_file = _wfopen(str::Utf8ToWide(path).c_str(), L"w");
#else
    g_file = std::fopen(path.c_str(), "w");
#endif
}

void Shutdown() {
    LockGuard lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void SetMinLevel(Level lvl) {
    LockGuard lock(g_mutex);
    g_minLevel = lvl;
}

const std::string& FilePath() { return g_path; }

void Write(Level lvl, const char* fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if OMM_WINDOWS
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);
    std::string line = str::Format("[%s][%s] %s", stamp, LevelTag(lvl), msg);

    LockGuard lock(g_mutex);
    if (lvl < g_minLevel) return;
    g_ring.push_back(line);
    while (g_ring.size() > kRingSize) g_ring.pop_front();
    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fputc('\n', g_file);
        std::fflush(g_file);
    }
#if OMM_WINDOWS
    OutputDebugStringA(("[OMM] " + line + "\n").c_str());
#endif
}

std::vector<std::string> Recent(size_t maxLines) {
    LockGuard lock(g_mutex);
    size_t n = g_ring.size() < maxLines ? g_ring.size() : maxLines;
    return std::vector<std::string>(g_ring.end() - static_cast<std::ptrdiff_t>(n), g_ring.end());
}

}  // namespace omm::log
