// The mod's own persistent settings (OutlastModMenu/config.ini).
// Thread-safe; values are stored as strings in an IniDocument.
#pragma once

#include "ini.h"
#include "sync.h"

#include <string>
#include <vector>

namespace omm {

class Settings {
public:
    static Settings& Get();

    bool Load(const std::string& path);
    bool Save();
    // Saves if something changed and at least `minIntervalMs` elapsed since
    // the last change (called periodically).
    void SaveIfDirty(uint64_t nowMs, uint64_t minIntervalMs = 1500);

    bool GetBool(const char* section, const char* key, bool def);
    int GetInt(const char* section, const char* key, int def);
    float GetFloat(const char* section, const char* key, float def);
    std::string GetString(const char* section, const char* key, const std::string& def);
    std::vector<std::pair<std::string, std::string>> GetSection(const char* section);

    void SetBool(const char* section, const char* key, bool v);
    void SetInt(const char* section, const char* key, int v);
    void SetFloat(const char* section, const char* key, float v);
    void SetString(const char* section, const char* key, const std::string& v);
    void RemoveKey(const char* section, const char* key);

    const std::string& Path() const { return path_; }

private:
    void Touch(uint64_t nowMs);

    Mutex mutex_;
    IniDocument doc_;
    std::string path_;
    uint64_t lastChangeMs_ = 0;
    bool pending_ = false;
};

uint64_t NowMs();

}  // namespace omm
