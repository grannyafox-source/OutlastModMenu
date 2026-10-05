#include "settings.h"

#include "fileutil.h"
#include "log.h"
#include "strutil.h"

#include <chrono>

namespace omm {

uint64_t NowMs() {
#if OMM_WINDOWS
    return GetTickCount64();
#else
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

Settings& Settings::Get() {
    static Settings s;
    return s;
}

bool Settings::Load(const std::string& path) {
    LockGuard lock(mutex_);
    path_ = path;
    if (!fs::Exists(path)) {
        doc_.Parse("; Outlast Mod Menu settings. Edited by the in-game menu.\r\n");
        return false;
    }
    bool ok = doc_.Load(path);
    if (!ok) LOGW("Could not read settings file %s", path.c_str());
    return ok;
}

bool Settings::Save() {
    LockGuard lock(mutex_);
    if (path_.empty()) return false;
    fs::CreateDirectories(fs::Parent(path_));
    bool ok = doc_.Save(path_);
    if (ok) {
        doc_.ClearDirty();
        pending_ = false;
    } else {
        LOGW("Could not write settings file %s", path_.c_str());
    }
    return ok;
}

void Settings::SaveIfDirty(uint64_t nowMs, uint64_t minIntervalMs) {
    bool doSave = false;
    {
        LockGuard lock(mutex_);
        doSave = pending_ && nowMs - lastChangeMs_ >= minIntervalMs;
    }
    if (doSave) Save();
}

void Settings::Touch(uint64_t nowMs) {
    pending_ = true;
    lastChangeMs_ = nowMs;
}

bool Settings::GetBool(const char* section, const char* key, bool def) {
    LockGuard lock(mutex_);
    auto v = doc_.Get(section, key);
    bool out = def;
    if (v && str::ParseBool(*v, out)) return out;
    return def;
}

int Settings::GetInt(const char* section, const char* key, int def) {
    LockGuard lock(mutex_);
    auto v = doc_.Get(section, key);
    int out = def;
    if (v && str::ParseInt(*v, out)) return out;
    return def;
}

float Settings::GetFloat(const char* section, const char* key, float def) {
    LockGuard lock(mutex_);
    auto v = doc_.Get(section, key);
    float out = def;
    if (v && str::ParseFloat(*v, out)) return out;
    return def;
}

std::string Settings::GetString(const char* section, const char* key, const std::string& def) {
    LockGuard lock(mutex_);
    auto v = doc_.Get(section, key);
    return v ? *v : def;
}

std::vector<std::pair<std::string, std::string>> Settings::GetSection(const char* section) {
    LockGuard lock(mutex_);
    return doc_.Entries(section);
}

void Settings::SetBool(const char* section, const char* key, bool v) { SetString(section, key, v ? "true" : "false"); }
void Settings::SetInt(const char* section, const char* key, int v) { SetString(section, key, std::to_string(v)); }
void Settings::SetFloat(const char* section, const char* key, float v) { SetString(section, key, str::FloatToIni(v)); }

void Settings::SetString(const char* section, const char* key, const std::string& v) {
    LockGuard lock(mutex_);
    if (doc_.Set(section, key, v)) Touch(NowMs());
}

void Settings::RemoveKey(const char* section, const char* key) {
    LockGuard lock(mutex_);
    if (doc_.Remove(section, key) > 0) Touch(NowMs());
}

}  // namespace omm
