#include "actions.h"

#include "data/checkpoints.h"
#include "outlast.h"
#include "state.h"

#include "../core/log.h"
#include "../core/settings.h"
#include "../core/strutil.h"
#include "../core/sync.h"
#include "../ue3/call.h"

#include <algorithm>
#include <cstdlib>
#include <deque>

namespace omm::game::actions {

using namespace ue3;

namespace {
constexpr const char* kPositions = "Positions";

Mutex g_consoleLock;
std::deque<std::string> g_console;

Mutex g_cpLock;
std::vector<std::string> g_gameCheckpoints;

void ConsoleAppend(const std::string& line) {
    LockGuard lock(g_consoleLock);
    g_console.push_back(line);
    while (g_console.size() > 400) g_console.pop_front();
}

bool NeedHero() {
    if (W().hero) return true;
    Notify("Start or load a game first.");
    return false;
}

bool NeedCheats() {
    if (EnsureCheats()) return true;
    Notify("Could not enable the game's cheat manager.");
    return false;
}

std::string Sanitize(std::string s) {
    for (char& c : s)
        if (c == '|' || c == '\r' || c == '\n' || c == '=' || c == ';') c = ' ';
    s = str::Trim(s);
    return s.empty() ? std::string("Position") : s.substr(0, 60);
}

std::string MapKey() {
    std::string m = MapName();
    return m.empty() ? std::string("unknown") : m;
}

bool CurrentPose(FVector& loc, FRotator& rot) {
    UObject* pc = W().pc;
    if (!pc) return false;
    // In noclip / free camera the camera is the interesting position.
    if (Bool(pc, "bDebugGhost") || Bool(pc, "bDebugFreeCam") || Bool(pc, "bDebugFixedCam")) {
        return GetVector(pc, "DebugCamPos", loc) && GetRotator(pc, "DebugCamRot", rot);
    }
    if (!W().hero || !GetVector(W().hero, "Location", loc)) return false;
    if (!GetRotator(pc, "Rotation", rot)) GetRotator(W().hero, "Rotation", rot);
    rot.Pitch = 0;
    rot.Roll = 0;
    return true;
}

std::string EncodePose(const std::string& label, const FVector& l, const FRotator& r) {
    return str::Format("%s|%.1f|%.1f|%.1f|%d|%d|%d", label.c_str(), l.X, l.Y, l.Z, r.Pitch, r.Yaw, r.Roll);
}

bool DecodePose(const std::string& v, std::string& label, FVector& l, FRotator& r) {
    std::vector<std::string> p = str::Split(v, '|');
    if (p.size() != 7) return false;
    label = p[0];
    int pitch = 0, yaw = 0, roll = 0;
    bool ok = str::ParseFloat(p[1], l.X) && str::ParseFloat(p[2], l.Y) && str::ParseFloat(p[3], l.Z) &&
              str::ParseInt(p[4], pitch) && str::ParseInt(p[5], yaw) && str::ParseInt(p[6], roll);
    r = FRotator{pitch, yaw, roll};
    return ok;
}

float Frand() { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); }
}  // namespace

// --- Cameras ---------------------------------------------------------------------

void ToggleNoclip() {
    if (!NeedHero() || !NeedCheats()) return;
    if (CheatExec("Ghost")) Notify(Bool(W().pc, "bDebugGhost") ? "Noclip ON" : "Noclip OFF");
}

void ToggleFreecam(bool pauseGame) {
    if (!NeedHero() || !NeedCheats()) return;
    if (CheatExec(pauseGame ? "ToggleFreeCam" : "ToggleFreeCamNoPause"))
        Notify(Bool(W().pc, "bDebugFreeCam") ? "Free camera ON" : "Free camera OFF");
}

void ToggleFixedCam() {
    if (!NeedHero() || !NeedCheats()) return;
    if (CheatExec("ToggleFixedCam")) Notify(Bool(W().pc, "bDebugFixedCam") ? "Fixed camera ON" : "Fixed camera OFF");
}

void TeleportToFreecam() {
    if (!NeedHero() || !NeedCheats()) return;
    UObject* pc = W().pc;
    if (!Bool(pc, "bDebugFreeCam") && !Bool(pc, "bDebugFixedCam")) {
        Notify("Turn on the free camera first.");
        return;
    }
    CheatExec("TeleportToFreeCam");
}

void ExitAllCameraModes() {
    UObject* pc = W().pc;
    if (!pc || !EnsureCheats()) return;
    if (Bool(pc, "bDebugGhost")) CheatExec("Ghost");
    if (Bool(pc, "bDebugFreeCam")) CheatExec("ToggleFreeCamNoPause");
    if (Bool(pc, "bDebugFixedCam")) CheatExec("ToggleFixedCam");
}

// --- Player ----------------------------------------------------------------------

void Heal() {
    if (!NeedHero()) return;
    UObject* hero = W().hero;
    int32_t maxHealth = Int(hero, "HealthMax", 100);
    SetInt(hero, "Health", maxHealth);
    SetFloat(hero, "PreciseHealth", static_cast<float>(maxHealth));
    Notify("Healed");
}

void KillPlayer() {
    if (!NeedHero()) return;
    if (EnsureCheats() && FindFunction(ClassOf(W().cheat), "KillPlayer")) {
        CheatExec("KillPlayer");
        return;
    }
    if (!CallNoArgs(W().hero, "Suicide")) Notify("This game version has no suicide function.");
}

void AddBatteries(int count) {
    UObject* pc = W().pc;
    if (!pc || !NeedHero()) return;
    int32_t now = Int(pc, "NumBatteries") + count;
    if (now < 0) now = 0;
    if (now > Int(pc, "MaxNumBatteries", 10)) SetInt(pc, "MaxNumBatteries", now);
    SetInt(pc, "NumBatteries", now);
    Notify(str::Format("Batteries: %d", now));
}

void RefillCurrentBattery() {
    if (!NeedHero()) return;
    SetFloat(W().hero, "CurrentBatterySetEnergy", 1.f);
    Notify("Battery refilled");
}

void GiveCamcorder(bool give) {
    if (!W().pc) return;
    SetBool(W().pc, "bHasCamcorder", give);
    Notify(give ? "You have the camcorder" : "Camcorder removed");
}

void TeleportToCrosshair() {
    if (!NeedHero()) return;
    FVector hit;
    if (!TraceFromCamera(30000.f, hit)) {
        Notify("Nothing under the crosshair.");
        return;
    }
    hit.Z += 50.f;
    TeleportPlayer(hit, nullptr);
}

void SetHeroScale(float scale) {
    if (!NeedHero()) return;
    UObject* hero = W().hero;
    scale = Clamp(scale, 0.1f, 5.f);
    Call c(hero, "SetDrawScale");
    if (c.Ok()) c.Float("NewScale", scale).Invoke();
    else SetFloat(hero, "DrawScale", scale);
}

const char* OutfitName(Outfit o) {
    switch (o) {
        case Outfit::Original: return "Original";
        case Outfit::Fingerless: return "Miles - after Trager (missing fingers)";
        case Outfit::ITTech: return "Waylon - IT technician uniform";
        case Outfit::Prisoner: return "Waylon - patient clothes";
    }
    return "?";
}

bool SetOutfit(Outfit o) {
    if (!NeedHero()) return false;
    UObject* hero = W().hero;
    // Remember the model the game gave the hero, so "Original" can go back to
    // it. If the game itself changed the model since our last swap (e.g. the
    // story's fingerless hands), that becomes the original.
    static UObject* s_hero = nullptr;
    static UObject* s_original = nullptr;
    static UObject* s_applied = nullptr;
    UObject* current = Obj(Obj(hero, "Mesh"), "SkeletalMesh");
    if (s_hero != hero || current != s_applied) {
        s_hero = hero;
        s_original = current;
    }
    UObject* mesh = nullptr;
    switch (o) {
        case Outfit::Original: {
            mesh = s_original && IsValid(s_original) ? s_original : nullptr;
            if (!mesh) {
                // The class default holds the mesh the hero spawns with.
                UObject* def = FindObject(("Default__" + Name(ClassOf(hero))).c_str());
                mesh = Obj(Obj(def, "Mesh"), "SkeletalMesh");
            }
            break;
        }
        case Outfit::Fingerless: mesh = Obj(hero, "FingerlessMesh"); break;
        case Outfit::ITTech: mesh = Obj(hero, "ITTechMesh"); break;
        case Outfit::Prisoner: mesh = Obj(hero, "PrisonerMesh"); break;
    }
    if (!mesh) {
        Notify("That outfit is not loaded in this part of the game.");
        return false;
    }
    bool ok = SetSkeletalMesh(Obj(hero, "Mesh"), mesh);
    if (UObject* shadow = Obj(hero, "ShadowProxy")) SetSkeletalMesh(shadow, mesh);
    if (ok) {
        s_applied = mesh;
        Notify(std::string("Outfit: ") + OutfitName(o));
    }
    return ok;
}

const std::vector<CharacterPreset>& CharacterPresets() {
    static const std::vector<CharacterPreset> presets = {
        {"Healthy", "Clears every injury effect set by the mod."},
        {"Limping", "Miles' limp, as at the end of the game before the Walrider."},
        {"Badly hurt", "A heavy hobble: slow, uneven steps."},
        {"Broken camcorder", "Cracked lens like after losing the camcorder in the Female Ward."},
        {"Wrecked", "Hobbling with a cracked lens and no film grain relief."},
    };
    return presets;
}

void ApplyCharacterPreset(int index) {
    ModState& s = state::Ui();
    s.walkingStyleOverride = false;
    s.limp = false;
    s.hobble = false;
    s.cameraCracked = false;
    switch (index) {
        case 1:
            s.limp = true;
            s.walkingStyleOverride = true;
            s.walkingStyle = 2;  // HWS_Limping
            break;
        case 2:
            s.hobble = true;
            s.hobbleIntensity = 0.85f;
            s.walkingStyleOverride = true;
            s.walkingStyle = 3;  // HWS_Hobbling
            break;
        case 3: s.cameraCracked = true; break;
        case 4:
            s.hobble = true;
            s.hobbleIntensity = 1.f;
            s.walkingStyleOverride = true;
            s.walkingStyle = 3;
            s.cameraCracked = true;
            break;
        default: break;
    }
    state::Publish();
}

// --- Teleport --------------------------------------------------------------------

std::vector<SavedPosition> SavedPositions(const std::string& map) {
    std::vector<SavedPosition> out;
    for (const auto& kv : Settings::Get().GetSection(kPositions)) {
        size_t bar = kv.first.find('|');
        if (bar == std::string::npos) continue;
        SavedPosition p;
        p.key = kv.first;
        p.map = kv.first.substr(0, bar);
        if (!map.empty() && !str::IEquals(p.map, map)) continue;
        if (!DecodePose(kv.second, p.label, p.location, p.rotation)) continue;
        out.push_back(p);
    }
    return out;
}

void SavePosition(const std::string& label) {
    FVector loc;
    FRotator rot;
    if (!CurrentPose(loc, rot)) {
        Notify("No position to save yet.");
        return;
    }
    std::string map = MapKey();
    int n = 1;
    Settings& cfg = Settings::Get();
    while (!cfg.GetString(kPositions, str::Format("%s|%d", map.c_str(), n).c_str(), "").empty()) ++n;
    cfg.SetString(kPositions, str::Format("%s|%d", map.c_str(), n).c_str(), EncodePose(Sanitize(label), loc, rot));
    Notify("Position saved: " + Sanitize(label));
}

void DeletePosition(const std::string& key) { Settings::Get().RemoveKey(kPositions, key.c_str()); }

bool GoToPosition(const SavedPosition& p) {
    if (!NeedHero()) return false;
    if (!p.map.empty() && !str::IEquals(p.map, MapKey())) {
        Notify("That position was saved in another level (" + p.map + ").");
        return false;
    }
    return TeleportPlayer(p.location, &p.rotation);
}

void QuickSavePosition() {
    FVector loc;
    FRotator rot;
    if (!CurrentPose(loc, rot)) return;
    Settings::Get().SetString(kPositions, (MapKey() + "|quick").c_str(), EncodePose("Quick save", loc, rot));
    Notify("Position saved (F7 to return)");
}

void QuickLoadPosition() {
    if (!NeedHero()) return;
    std::string v = Settings::Get().GetString(kPositions, (MapKey() + "|quick").c_str(), "");
    SavedPosition p;
    if (v.empty() || !DecodePose(v, p.label, p.location, p.rotation)) {
        Notify("No quick-saved position in this level (F6 saves one).");
        return;
    }
    p.map = MapKey();
    if (GoToPosition(p)) Notify("Returned to saved position");
}

bool TeleportRandomLocation() {
    if (!NeedHero()) return false;
    UObject* hero = W().hero;
    FVector here;
    GetVector(hero, "Location", here);
    // Spots designed to be stood on come first; markers and pickups are a
    // fallback (they can sit on furniture).
    std::vector<FVector> good, fallback;
    UClass* nav = FindClass("NavigationPoint");
    UClass* marker = FindClass("OLGameplayMarker");
    UClass* pickup = FindClass("OLPickableObject");
    ForEachObject([&](UObject* o) {
        bool isNav = nav && IsA(o, nav);
        bool isOther = !isNav && ((marker && IsA(o, marker)) || (pickup && IsA(o, pickup)));
        if (!isNav && !isOther) return true;
        if (IsDefaultObject(o) || !Obj(o, "WorldInfo") || Bool(o, "bDeleteMe")) return true;
        FVector l;
        if (!GetVector(o, "Location", l) || (l - here).Size() < 600.f) return true;
        (isNav ? good : fallback).push_back(l);
        return good.size() + fallback.size() < 20000;
    });
    std::vector<FVector>& pool = good.size() >= 3 ? good : (good.empty() ? fallback : good);
    if (pool.empty()) {
        Notify("No safe spots found in this level.");
        return false;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        FVector dest = pool[static_cast<size_t>(std::rand()) % pool.size()];
        dest.Z += (&pool == &good) ? 10.f : 80.f;
        FRotator rot{0, static_cast<int32_t>(Frand() * 65535.f), 0};
        TeleportPlayer(dest, &rot);
        FVector after;
        GetVector(hero, "Location", after);
        if ((after - dest).Size() < 150.f || Bool(W().pc, "bDebugGhost")) {
            Notify("Teleported somewhere random...");
            return true;
        }
    }
    Notify("Random teleport failed (blocked) - try again.");
    return false;
}

bool LoadCheckpoint(const std::string& name) {
    if (!W().pc) {
        Notify("The game is not ready yet.");
        return false;
    }
    ExitAllCameraModes();
    bool ok = game::LoadCheckpoint(name);
    Notify(ok ? "Loading " + name + "..." : "Could not load " + name);
    return ok;
}

std::string RandomCheckpoint(bool includeMain, bool includeDlc) {
    std::vector<const char*> pool;
    for (const data::Chapter& c : data::Chapters())
        if ((c.dlc && includeDlc) || (!c.dlc && includeMain))
            for (const data::Checkpoint& cp : c.checkpoints) pool.push_back(cp.name);
    return pool.empty() ? std::string() : pool[static_cast<size_t>(std::rand()) % pool.size()];
}

std::string RandomScene(bool includeMain, bool includeDlc) {
    std::vector<const char*> pool;
    for (const data::Scene& s : data::Scenes())
        if ((s.dlc && includeDlc) || (!s.dlc && includeMain)) pool.push_back(s.checkpoint);
    return pool.empty() ? std::string() : pool[static_cast<size_t>(std::rand()) % pool.size()];
}

std::vector<std::string> GameCheckpointList() {
    LockGuard lock(g_cpLock);
    return g_gameCheckpoints;
}

void RefreshGameCheckpointList() {
    std::vector<std::string> names;
    if (UClass* cls = FindClass("OLCheckpointList")) {
        for (UObject* list : FindInstances(cls, 16)) {
            std::vector<FName> cps;
            if (!GetNameArray(list, "CheckpointList", cps)) continue;
            for (const FName& n : cps) {
                std::string s = NameToString(n);
                if (!s.empty() && std::find(names.begin(), names.end(), s) == names.end()) names.push_back(s);
            }
        }
    }
    LockGuard lock(g_cpLock);
    g_gameCheckpoints.swap(names);
}

// --- Console ---------------------------------------------------------------------

void RunConsoleCommand(const std::string& command) {
    std::string cmd = str::Trim(command);
    if (cmd.empty()) return;
    ConsoleAppend("> " + cmd);
    std::string out = Console(cmd);
    for (const std::string& line : str::Split(out, '\n', false))
        if (!str::Trim(line).empty()) ConsoleAppend(line);
}

std::vector<std::string> ConsoleLog() {
    LockGuard lock(g_consoleLock);
    return std::vector<std::string>(g_console.begin(), g_console.end());
}

}  // namespace omm::game::actions
