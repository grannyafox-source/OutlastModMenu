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
// Identical consecutive messages are collapsed into one line plus a count.
std::string g_lastMessage;
int g_repeats = 0;

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
    if (g_lastMessage == msg) {
        ++g_repeats;
        return;
    }
    std::vector<std::string> out;
    if (g_repeats) out.push_back(str::Format("[%s][INF] (previous message repeated %d more time%s)", stamp, g_repeats,
                                             g_repeats == 1 ? "" : "s"));
    out.push_back(line);
    g_lastMessage = msg;
    g_repeats = 0;
    for (const std::string& l : out) {
        g_ring.push_back(l);
        while (g_ring.size() > kRingSize) g_ring.pop_front();
        if (g_file) {
            std::fputs(l.c_str(), g_file);
            std::fputc('\n', g_file);
        }
#if OMM_WINDOWS
        OutputDebugStringA(("[OMM] " + l + "\n").c_str());
#endif
    }
    if (g_file) std::fflush(g_file);
}

std::vector<std::string> Recent(size_t maxLines) {
    LockGuard lock(g_mutex);
    size_t n = g_ring.size() < maxLines ? g_ring.size() : maxLines;
    return std::vector<std::string>(g_ring.end() - static_cast<std::ptrdiff_t>(n), g_ring.end());
}

}  // namespace omm::log
