#include "initweaks.h"

#include "../core/fileutil.h"
#include "../core/ini.h"
#include "../core/log.h"
#include "../core/paths.h"
#include "../core/settings.h"
#include "../core/strutil.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <optional>
#include <map>

namespace omm::game::ini {

namespace {
constexpr const char* kOriginals = "IniOriginal";  // file|section|key -> original value
constexpr const char* kActive = "IniTweaks";       // tweak id -> on
constexpr const char* kAbsent = "<<absent>>";

using F = File;
constexpr const char* SS = "SystemSettings";

const std::vector<Tweak> kTweaks = {
    // --- Performance (low-end PCs and laptops) -----------------------------------
    {"perf_shadows_cheap", "Performance", "Cheaper shadows",
     "Lower shadow resolution and no whole-scene shadows. Keeps the atmosphere, costs much less.",
     {{F::SystemSettings, SS, "MaxShadowResolution", "512"},
      {F::SystemSettings, SS, "MaxWholeSceneDominantShadowResolution", "512"},
      {F::SystemSettings, SS, "bAllowWholeSceneDominantShadows", "False"},
      {F::SystemSettings, SS, "ShadowFilterQualityBias", "-1"}}},
    {"perf_shadows_off", "Performance", "No dynamic shadows",
     "Turns dynamic shadows off completely. Biggest speed-up on weak GPUs, but scenes look flatter.",
     {{F::SystemSettings, SS, "DynamicShadows", "False"}, {F::SystemSettings, SS, "LightEnvironmentShadows", "False"}}},
    {"perf_postfx", "Performance", "No expensive post-processing",
     "Disables motion blur, depth of field, ambient occlusion, light shafts and lens flares.",
     {{F::SystemSettings, SS, "MotionBlur", "False"},
      {F::SystemSettings, SS, "DepthOfField", "False"},
      {F::SystemSettings, SS, "AmbientOcclusion", "False"},
      {F::SystemSettings, SS, "bAllowLightShafts", "False"},
      {F::SystemSettings, SS, "LensFlares", "False"}}},
    {"perf_distortion", "Performance", "No heat/glass distortion",
     "Disables refraction effects (fire haze, broken glass).",
     {{F::SystemSettings, SS, "Distortion", "False"},
      {F::SystemSettings, SS, "FilteredDistortion", "False"},
      {F::SystemSettings, SS, "DropParticleDistortion", "True"}}},
    {"perf_detail", "Performance", "Low detail",
     "Low detail mode, simpler character/particle LODs, fewer decals and lower texture filtering.",
     {{F::SystemSettings, SS, "DetailMode", "0"},
      {F::SystemSettings, SS, "SkeletalMeshLODBias", "1"},
      {F::SystemSettings, SS, "ParticleLODBias", "1"},
      {F::SystemSettings, SS, "DynamicDecals", "False"},
      {F::SystemSettings, SS, "DecalCullDistanceScale", "0.5"},
      {F::SystemSettings, SS, "MaxAnisotropy", "2"}}},
    {"perf_scale75", "Performance", "Render at 75% resolution",
     "Renders the 3D scene at 75% and upscales it. Menus and text stay sharp.",
     {{F::SystemSettings, SS, "ScreenPercentage", "75.000000"}, {F::SystemSettings, SS, "UpscaleScreenPercentage", "True"}}},
    {"perf_scale60", "Performance", "Render at 60% resolution (potato mode)",
     "For integrated graphics. Blurry but much faster.",
     {{F::SystemSettings, SS, "ScreenPercentage", "60.000000"}, {F::SystemSettings, SS, "UpscaleScreenPercentage", "True"}}},
    {"perf_no_aa", "Performance", "No FXAA", "Turns off the FXAA anti-aliasing pass.",
     {{F::SystemSettings, SS, "bAllowPostprocessFXAA", "False"}}},
    {"perf_vsync_off", "Performance", "VSync off", "Lower input lag and higher frame rates (may tear).",
     {{F::SystemSettings, SS, "UseVsync", "False"}}},

    // --- Visual quality -------------------------------------------------------------
    {"qual_aniso", "Visuals", "16x anisotropic filtering", "Sharper floors and walls at an angle. Almost free.",
     {{F::SystemSettings, SS, "MaxAnisotropy", "16"}}},
    {"qual_shadows", "Visuals", "High-resolution shadows", "Crisper shadows (needs a decent GPU).",
     {{F::SystemSettings, SS, "MaxShadowResolution", "2048"},
      {F::SystemSettings, SS, "MaxWholeSceneDominantShadowResolution", "2048"}}},
    {"qual_texpool", "Visuals", "Bigger texture pool",
     "Lets the engine keep more high-resolution textures in memory: less blurry pop-in (2 GB+ VRAM).",
     {{F::Engine, "TextureStreaming", "PoolSize", "1024"}}},
    {"qual_no_mblur", "Visuals", "No motion blur", "Removes motion blur only.",
     {{F::SystemSettings, SS, "MotionBlur", "False"}}},
    {"qual_no_dof", "Visuals", "No depth of field", "Removes the background blur.",
     {{F::SystemSettings, SS, "DepthOfField", "False"}}},
    {"vis_fps144", "Visuals", "Raise FPS cap to 144", "The game is capped at 62 FPS by default.",
     {{F::Engine, "Engine.Engine", "MaxSmoothedFrameRate", "144"}}},
    {"vis_fps_unlocked", "Visuals", "Unlimited FPS", "Removes the frame-rate cap entirely.",
     {{F::Engine, "Engine.Engine", "bSmoothFrameRate", "FALSE"}}},
    {"vis_skip_intro", "Visuals", "Skip intro logo movie", "Starts straight at the main menu.",
     {{F::Engine, "FullScreenMovie", "StartupMovies", nullptr}}},

    // --- Controls ---------------------------------------------------------------------
    {"ctl_raw_mouse", "Controls", "Raw mouse (no smoothing)", "Removes the floaty mouse smoothing.",
     {{F::Input, "Engine.PlayerInput", "bEnableMouseSmoothing", "false"},
      {F::Input, "OLGame.OLPlayerInput", "bEnableMouseSmoothing", "false"}}},

    // --- Gameplay ---------------------------------------------------------------------
    {"game_cheats", "Gameplay", "Enable the game's cheat manager",
     "Turns on Outlast's built-in debug cheats (the mod also does this while it runs).",
     {{F::Game, "OLGame.OLCheatManager", "bCheatsEnabled", "true"}}},
    {"game_batteries", "Gameplay", "Unlimited batteries (config)",
     "The game's own unlimited-battery switch, active even without the mod.",
     {{F::Game, "OLGame.OLCheatManager", "bUnlimitedBatteries", "true"}}},
    {"game_more_battery_slots", "Gameplay", "Carry more batteries",
     "Battery capacity 20 (Normal), 12 (Hard) and 6 (Nightmare); start with 5.",
     {{F::Game, "OLGame.OLPlayerController", "NrmMaxNumBatteries", "20"},
      {F::Game, "OLGame.OLPlayerController", "HardMaxNumBatteries", "12"},
      {F::Game, "OLGame.OLPlayerController", "NightmareMaxNumBatteries", "6"},
      {F::Game, "OLGame.OLPlayerController", "DefaultNumBatteries", "5"}}},
    {"game_struggle", "Gameplay", "Struggles can't be failed",
     "The game's own accessibility patch: grab/struggle sequences use the easy thresholds.",
     {{F::Game, "Patches", "StruggleNoFail", "True"}}},
    {"game_no_tutorials", "Gameplay", "No tutorial pop-ups", "Hides the control hints shown during the first chapters.",
     {{F::Game, "OLGame.OLTutorialManager", "bTutorialsEnabled", "false"}}},
    {"game_long_batteries", "Gameplay", "Batteries last twice as long", "300 seconds per battery instead of 150.",
     {{F::Game, "OLGame.OLHero", "NrmBatteryDuration", "300.0"}, {F::Game, "OLGame.OLHero", "HardBatteryDuration", "300.0"}}},

    // --- Story / character ---------------------------------------------------------------
    {"story_fingerless", "Character", "Missing fingers from the start",
     "Miles uses his 'after Trager' hands from the first checkpoint instead of after the operating room.",
     {{F::Game, "OLGame.OLHero", "FirstFingerlessCheckpoint", "Admin_Gates"}}},
    {"story_cracked", "Character", "Cracked camcorder lens from the start",
     "The lens is broken from the beginning, like after the Female Ward.",
     {{F::Game, "OLGame.OLHero", "ShatteredCameraGlassCheckpoint", "Admin_Gates"}}},
    {"story_wb_prisoner", "Character", "Whistleblower: patient clothes from the start",
     "Waylon wears the patient outfit from the first DLC checkpoint.",
     {{F::Game, "OLGame.OLHero", "PrisonerUniformCheckpoint", "DLC_Start"}}},

    // --- Enemies ----------------------------------------------------------------------
    {"enemy_no_search", "Enemies", "Enemies never search hiding spots",
     "Lockers and beds are always safe (enemies no longer investigate them).",
     {{F::Enemy, "OLGame.OLEnemyPawn", "bInvestigateLockers", "false"},
      {F::Enemy, "OLGame.OLEnemyPawn", "bInvestigateBeds", "false"},
      {F::Enemy, "OLGame.OLEnemyPawn", "InvestigationFindHiddenPlayerProbability", "0.0"}}},
    {"enemy_hard_of_hearing", "Enemies", "Hard-of-hearing enemies", "Most enemies hear you from half the distance.",
     {{F::Enemy, "OLGame.OLEnemyPawn", "NrmEnemyHearingThreshold", "1000.0"},
      {F::Enemy, "OLGame.OLEnemyPawn", "HardEnemyHearingThreshold", "1500.0"}}},
    {"enemy_sharp_ears", "Enemies", "Sharp-eared enemies", "Most enemies hear you from much further away.",
     {{F::Enemy, "OLGame.OLEnemyPawn", "NrmEnemyHearingThreshold", "3500.0"},
      {F::Enemy, "OLGame.OLEnemyPawn", "HardEnemyHearingThreshold", "4500.0"}}},
};

const std::vector<Preset> kPresets = {
    {"Laptop / low-end PC", "Big performance gain while keeping the look mostly intact.",
     {"perf_shadows_cheap", "perf_postfx", "perf_distortion", "perf_detail", "perf_scale75", "vis_skip_intro"}},
    {"Potato", "Everything that costs frames is off. Use if the game is still slow.",
     {"perf_shadows_off", "perf_postfx", "perf_distortion", "perf_detail", "perf_scale60", "perf_no_aa",
      "vis_skip_intro"}},
    {"High quality", "For strong PCs.", {"qual_aniso", "qual_shadows", "qual_texpool", "vis_fps144"}},
    {"Clean image", "No blur effects, raw mouse, higher frame-rate cap.",
     {"qual_no_mblur", "qual_no_dof", "ctl_raw_mouse", "vis_fps144", "vis_skip_intro"}},
    {"Casual", "A gentler game: more batteries, struggles can't fail, safe hiding spots.",
     {"game_more_battery_slots", "game_long_batteries", "game_struggle", "enemy_no_search"}},
};

std::string RecordKey(const Change& c) {
    return str::Format("%d|%s|%s", static_cast<int>(c.file), c.section, c.key);
}

bool SameValue(const std::string& a, const std::string& b) {
    std::string x = str::Trim(a), y = str::Trim(b);
    if (str::IEquals(x, y)) return true;
    float fx = 0, fy = 0;
    if (str::ParseFloat(x, fx) && str::ParseFloat(y, fy)) return std::fabs(fx - fy) < 1e-4f;
    bool bx = false, by = false;
    if (str::ParseBool(x, bx) && str::ParseBool(y, by)) return bx == by;
    return false;
}

bool Matches(const IniDocument& doc, const Change& c) {
    std::optional<std::string> v = doc.Get(c.section, c.key);
    if (!c.value) return !v.has_value();
    return v && SameValue(*v, c.value);
}

struct Docs {
    std::map<int, IniDocument> docs;
    std::map<int, bool> loaded;
    IniDocument* Get(File f) {
        int i = static_cast<int>(f);
        if (!loaded.count(i)) loaded[i] = docs[i].Load(FilePath(f));
        return loaded[i] ? &docs[i] : nullptr;
    }
    bool SaveAll(std::string& error) {
        bool ok = true;
        for (auto& kv : docs) {
            if (!loaded[kv.first] || !kv.second.Dirty()) continue;
            std::string path = FilePath(static_cast<File>(kv.first));
            if (!kv.second.Save(path)) {
                error = "could not write " + path;
                ok = false;
            } else {
                kv.second.ClearDirty();
            }
        }
        return ok;
    }
};

bool IsActive(const std::string& id) { return Settings::Get().GetBool(kActive, id.c_str(), false); }

// Another active tweak (other than `self`) that sets the same key.
const Change* OtherActiveChange(const Tweak& self, const Change& c) {
    for (const Tweak& t : kTweaks) {
        if (&t == &self || !IsActive(t.id)) continue;
        for (const Change& o : t.changes)
            if (o.file == c.file && str::IEquals(o.section, c.section) && str::IEquals(o.key, c.key)) return &o;
    }
    return nullptr;
}

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if OMM_WINDOWS
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
    return buf;
}

std::string BackupRoot() { return paths::ModSubdir("ini_backup"); }

bool CopyConfigTo(const std::string& dest, std::string& error) {
    if (!fs::CreateDirectories(dest)) {
        error = "could not create " + dest;
        return false;
    }
    int copied = 0;
    for (const fs::DirEntry& e : fs::List(paths::UserConfigDir())) {
        if (e.isDir || fs::Extension(e.name) != ".ini") continue;
        if (fs::CopyFileTo(e.path, fs::Join(dest, e.name), true)) ++copied;
    }
    if (!copied) {
        error = "no .ini files found in " + paths::UserConfigDir();
        return false;
    }
    return true;
}
}  // namespace

const char* FileName(File f) {
    switch (f) {
        case File::Engine: return "OLEngine.ini";
        case File::Game: return "OLGame.ini";
        case File::Input: return "OLInput.ini";
        case File::SystemSettings: return "OLSystemSettings.ini";
        case File::Enemy: return "OLEnemy.ini";
        default: return "?";
    }
}

std::string FilePath(File f) { return paths::ConfigFile(FileName(f)); }

const std::vector<Tweak>& Tweaks() { return kTweaks; }
const std::vector<Preset>& Presets() { return kPresets; }

const Tweak* FindTweak(const std::string& id) {
    for (const Tweak& t : kTweaks)
        if (id == t.id) return &t;
    return nullptr;
}

TweakState StateOf(const Tweak& t) {
    Docs docs;
    int on = 0;
    for (const Change& c : t.changes) {
        IniDocument* d = docs.Get(c.file);
        if (d && Matches(*d, c)) ++on;
    }
    if (on == 0) return TweakState::Off;
    return on == static_cast<int>(t.changes.size()) ? TweakState::On : TweakState::Partial;
}

bool Apply(const Tweak& t, std::string& error) {
    EnsureInitialBackup();
    Docs docs;
    Settings& cfg = Settings::Get();
    for (const Change& c : t.changes) {
        IniDocument* d = docs.Get(c.file);
        if (!d) {
            error = std::string("could not read ") + FilePath(c.file);
            return false;
        }
        std::string rk = RecordKey(c);
        if (cfg.GetString(kOriginals, rk.c_str(), "").empty()) {
            std::optional<std::string> cur = d->Get(c.section, c.key);
            cfg.SetString(kOriginals, rk.c_str(), cur ? *cur : std::string(kAbsent));
        }
        if (c.value) d->Set(c.section, c.key, c.value);
        else d->Remove(c.section, c.key);
    }
    if (!docs.SaveAll(error)) return false;
    cfg.SetBool(kActive, t.id, true);
    cfg.Save();
    LOGI("INI tweak applied: %s", t.id);
    return true;
}

bool Revert(const Tweak& t, std::string& error) {
    Docs docs;
    Settings& cfg = Settings::Get();
    for (const Change& c : t.changes) {
        IniDocument* d = docs.Get(c.file);
        if (!d) {
            error = std::string("could not read ") + FilePath(c.file);
            return false;
        }
        if (const Change* other = OtherActiveChange(t, c)) {
            if (other->value) d->Set(c.section, c.key, other->value);
            else d->Remove(c.section, c.key);
            continue;
        }
        std::string rk = RecordKey(c);
        std::string orig = cfg.GetString(kOriginals, rk.c_str(), "");
        if (orig.empty()) continue;  // never changed by us
        if (orig == kAbsent) d->Remove(c.section, c.key);
        else d->Set(c.section, c.key, orig);
        cfg.RemoveKey(kOriginals, rk.c_str());
    }
    if (!docs.SaveAll(error)) return false;
    cfg.RemoveKey(kActive, t.id);
    cfg.Save();
    LOGI("INI tweak reverted: %s", t.id);
    return true;
}

int ApplyPreset(const Preset& p, std::string& error) {
    int n = 0;
    for (const char* id : p.tweaks) {
        const Tweak* t = FindTweak(id);
        if (t && Apply(*t, error)) ++n;
    }
    return n;
}

std::string BackupNow(std::string& error) {
    std::string dir = fs::Join(BackupRoot(), Timestamp());
    return CopyConfigTo(dir, error) ? dir : std::string();
}

std::vector<std::string> Backups() {
    std::vector<std::string> out;
    for (const fs::DirEntry& e : fs::List(BackupRoot()))
        if (e.isDir) out.push_back(e.name);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        if (a == "original") return true;  // first-run backup on top
        if (b == "original") return false;
        return a > b;
    });
    return out;
}

bool RestoreBackup(const std::string& folder, std::string& error) {
    std::string dir = fs::Join(BackupRoot(), folder);
    if (folder.find("..") != std::string::npos || !fs::IsDirectory(dir)) {
        error = "backup not found";
        return false;
    }
    std::string safety;
    BackupNow(safety);  // keep the current state too
    int restored = 0;
    for (const fs::DirEntry& e : fs::List(dir)) {
        if (e.isDir || fs::Extension(e.name) != ".ini") continue;
        if (fs::CopyFileTo(e.path, fs::Join(paths::UserConfigDir(), e.name), true)) ++restored;
    }
    if (!restored) {
        error = "nothing to restore";
        return false;
    }
    // The restored files no longer contain our edits.
    Settings& cfg = Settings::Get();
    for (const auto& kv : cfg.GetSection(kActive)) cfg.RemoveKey(kActive, kv.first.c_str());
    for (const auto& kv : cfg.GetSection(kOriginals)) cfg.RemoveKey(kOriginals, kv.first.c_str());
    cfg.Save();
    LOGI("Restored %d config files from %s", restored, dir.c_str());
    return true;
}

void EnsureInitialBackup() {
    std::string dir = fs::Join(BackupRoot(), "original");
    if (fs::IsDirectory(dir)) return;
    std::string error;
    if (CopyConfigTo(dir, error)) LOGI("Backed up the game's config files to %s", dir.c_str());
    else LOGW("Initial config backup failed: %s", error.c_str());
}

int ReapplyActive() {
    Docs docs;
    int fixed = 0;
    for (const Tweak& t : kTweaks) {
        if (!IsActive(t.id)) continue;
        for (const Change& c : t.changes) {
            IniDocument* d = docs.Get(c.file);
            if (!d || Matches(*d, c)) continue;
            if (c.value) d->Set(c.section, c.key, c.value);
            else d->Remove(c.section, c.key);
            ++fixed;
        }
    }
    std::string error;
    if (fixed && !docs.SaveAll(error)) LOGW("Re-applying INI tweaks failed: %s", error.c_str());
    if (fixed) LOGI("Re-applied %d INI value(s) the game had reset", fixed);
    return fixed;
}

std::string Read(File f, const std::string& section, const std::string& key) {
    IniDocument d;
    if (!d.Load(FilePath(f))) return std::string();
    return d.Get(section, key).value_or("");
}

bool Write(File f, const std::string& section, const std::string& key, const std::string& value, std::string& error) {
    EnsureInitialBackup();
    IniDocument d;
    if (!d.Load(FilePath(f))) {
        error = "could not read " + FilePath(f);
        return false;
    }
    d.Set(section, key, value);
    if (!d.Save(FilePath(f))) {
        error = "could not write " + FilePath(f);
        return false;
    }
    return true;
}

}  // namespace omm::game::ini
