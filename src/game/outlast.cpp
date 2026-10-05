#include "outlast.h"

#include "../core/log.h"
#include "../ue3/call.h"

#include <cmath>
#include <unordered_map>

namespace omm::game {

using namespace ue3;

namespace {
World g_world;
UObject* g_engine = nullptr;
std::string g_mapName;  // cached: GetMapName returns an engine-allocated string
bool g_mapNameValid = false;

UObject* ValidOrNull(UObject* o) { return IsValid(o) ? o : nullptr; }
}  // namespace

const World& W() { return g_world; }

UObject* FindEngine() {
    if (g_engine && IsValid(g_engine)) return g_engine;
    g_engine = nullptr;
    UClass* gameEngine = FindClass("GameEngine");
    if (!gameEngine) return nullptr;
    ForEachObject([&](UObject* o) {
        if (IsA(o, gameEngine) && !IsDefaultObject(o)) {
            g_engine = o;
            return false;
        }
        return true;
    });
    return g_engine;
}

void RefreshWorld(UObject* viewportClient) {
    World w;
    w.engine = FindEngine();
    w.viewport = viewportClient ? viewportClient : ValidOrNull(Obj(w.engine, "GameViewport"));
    std::vector<UObject*> players;
    if (w.engine && GetObjArray(w.engine, "GamePlayers", players) && !players.empty())
        w.localPlayer = ValidOrNull(players[0]);
    w.pc = ValidOrNull(Obj(w.localPlayer, "Actor"));
    if (w.pc) {
        w.hero = ValidOrNull(Obj(w.pc, "HeroPawn"));
        if (!w.hero) w.hero = ValidOrNull(Obj(w.pc, "Pawn"));
        if (w.hero && Bool(w.hero, "bDeleteMe")) w.hero = nullptr;
        w.cheat = ValidOrNull(Obj(w.pc, "CheatManager"));
        w.worldInfo = ValidOrNull(Obj(w.pc, "WorldInfo"));
        w.hud = ValidOrNull(Obj(w.pc, "myHUD"));
        w.fx = ValidOrNull(Obj(w.pc, "FXManager"));
    }
    if (w.worldInfo) w.game = ValidOrNull(Obj(w.worldInfo, "Game"));
    if (w.worldInfo != g_world.worldInfo || w.pc != g_world.pc) g_mapNameValid = false;
    g_world = w;
}

std::string Console(const std::string& command) {
    if (!g_world.pc) return "no player controller";
    Call c(g_world.pc, "ConsoleCommand");
    c.Str("Command", command);
    if (c.HasParam("bWriteToLog")) c.Bool("bWriteToLog", true);
    if (!c.Invoke()) return "failed: " + c.Error();
    LOGI("console> %s", command.c_str());
    return c.RetStr();
}

bool EnsureCheats() {
    UObject* pc = g_world.pc;
    if (!pc) return false;
    UObject* cm = ValidOrNull(Obj(pc, "CheatManager"));
    if (!cm) {
        Call add(pc, "AddCheats");
        if (add.Ok()) {
            if (add.HasParam("bForce")) add.Bool("bForce", true);
            add.Invoke();
        }
        cm = ValidOrNull(Obj(pc, "CheatManager"));
    }
    if (!cm) {
        CallNoArgs(pc, "EnableCheats");
        cm = ValidOrNull(Obj(pc, "CheatManager"));
    }
    if (!cm) return false;
    bool enabled = false;
    if (GetBool(cm, "bCheatsEnabled", enabled) && !enabled) SetBool(cm, "bCheatsEnabled", true);
    g_world.cheat = cm;
    return true;
}

bool CheatExec(const char* function) {
    if (!EnsureCheats()) return false;
    if (FindFunction(ClassOf(g_world.cheat), function)) return CallNoArgs(g_world.cheat, function);
    if (g_world.pc && FindFunction(ClassOf(g_world.pc), function)) return CallNoArgs(g_world.pc, function);
    LOGW("Exec function %s not found in this game version", function);
    return false;
}

bool TeleportPlayer(const FVector& loc, const FRotator* rot) {
    UObject* pc = g_world.pc;
    if (!pc) return false;
    // While flying (Ghost / free camera) the camera position is what moves.
    if (Bool(pc, "bDebugGhost") || Bool(pc, "bDebugFreeCam") || Bool(pc, "bDebugFixedCam")) {
        SetVector(pc, "DebugCamPos", loc);
        if (rot) SetRotator(pc, "DebugCamRot", *rot);
        return true;
    }
    UObject* hero = g_world.hero;
    if (!hero) return false;
    FRotator r{};
    if (rot) r = *rot;
    else GetRotator(hero, "Rotation", r);
    bool ok = false;
    Call csl(pc, "ClientSetLocation");
    if (csl.Ok()) {
        csl.Vector("NewLocation", loc).Rotator("NewRotation", r);
        ok = csl.Invoke();
    }
    if (!ok) {
        Call sl(hero, "SetLocation");
        sl.Vector("NewLocation", loc);
        ok = sl.Invoke();
    }
    SetVector(hero, "Velocity", FVector(0, 0, 0));
    if (rot) {
        SetRotator(hero, "EyeRotation", *rot);
        SetRotator(pc, "Rotation", *rot);
    }
    CallNoArgs(hero, "ResetAfterTeleport");
    return ok;
}

bool LoadCheckpoint(const std::string& name) {
    UObject* pc = g_world.pc;
    if (!pc || name.empty()) return false;
    Call c(pc, "StartNewGameAtCheckpoint");
    if (!c.Ok()) return false;
    c.Str("CheckpointStr", name);
    if (c.HasParam("bSaveToDisk")) c.Bool("bSaveToDisk", false);
    LOGI("Loading checkpoint %s", name.c_str());
    return c.Invoke();
}

bool GetViewPoint(FVector& loc, FRotator& rot, float& fov) {
    UObject* pc = g_world.pc;
    if (!pc) return false;
    Call c(pc, "GetPlayerViewPoint");
    if (!c.Ok() || !c.Invoke()) return false;
    // PlayerController names them POVLocation/POVRotation, Controller
    // out_Location/out_Rotation; go by type so any override works.
    std::string locParam = c.StructParam("Vector"), rotParam = c.StructParam("Rotator");
    if (locParam.empty() || rotParam.empty()) return false;
    loc = c.OutVector(locParam.c_str());
    rot = c.OutRotator(rotParam.c_str());
    Call f(pc, "GetFOVAngle");
    fov = (f.Ok() && f.Invoke()) ? f.RetFloat() : 90.f;
    if (!(fov > 1.f && fov < 179.f)) fov = 90.f;
    return true;
}

bool TraceFromCamera(float distance, FVector& hit) {
    FVector loc;
    FRotator rot;
    float fov;
    if (!GetViewPoint(loc, rot, fov) || !g_world.pc) return false;
    FVector dir = RotatorToVector(rot);
    FVector end = loc + dir * distance;
    Call t(g_world.pc, "Trace");
    if (!t.Ok()) return false;
    t.Vector("TraceEnd", end).Vector("TraceStart", loc);
    if (t.HasParam("bTraceActors")) t.Bool("bTraceActors", false);
    if (!t.Invoke()) return false;
    FVector h = t.OutVector("HitLocation");
    if (h.X == 0 && h.Y == 0 && h.Z == 0) return false;
    // Step back a little along the normal so the player does not end up
    // inside the wall.
    FVector n = t.OutVector("HitNormal");
    hit = h + n * 60.f;
    return true;
}

UObject* LoadObjectByPath(const std::string& path, const char* className) {
    UClass* cls = FindClass(className);
    // Already in memory?
    if (UObject* found = FindObjectByPath(path, className)) return found;
    if (!cls || !g_world.pc) return nullptr;
    Call c(g_world.pc, "DynamicLoadObject");
    if (!c.Ok()) return nullptr;
    c.Str("ObjectName", path).Obj("ObjectClass", cls);
    if (c.HasParam("MayFail")) c.Bool("MayFail", true);
    if (!c.Invoke()) return nullptr;
    UObject* o = c.RetObj();
    return IsValid(o) ? o : nullptr;
}

bool SetSkeletalMesh(UObject* comp, UObject* mesh) {
    if (!comp || !mesh) return false;
    Call c(comp, "SetSkeletalMesh");
    if (!c.Ok()) return false;
    c.Obj("NewMesh", mesh);
    return c.Invoke();
}

std::string MapName() {
    if (!g_world.worldInfo) return std::string();
    if (g_mapNameValid) return g_mapName;
    Call c(g_world.worldInfo, "GetMapName");
    if (!c.Ok()) return std::string();
    if (c.HasParam("bIncludePrefix")) c.Bool("bIncludePrefix", false);
    if (!c.Invoke()) return std::string();
    g_mapName = c.RetStr();
    g_mapNameValid = true;
    return g_mapName;
}

bool PlayingDLC() { return g_world.game && Bool(g_world.game, "bIsPlayingDLC"); }

std::string CurrentCheckpoint() {
    FName n;
    if (g_world.game && GetName(g_world.game, "CurrentCheckpointName", n) && n.Index) return NameToString(n);
    return std::string();
}

// ---------------------------------------------------------------------------
// OverrideStore

OverrideStore& Overrides() {
    static OverrideStore s;
    return s;
}

OverrideStore::Entry* OverrideStore::Begin(const std::string& key, UObject* obj, const char* path, bool enable,
                                           Type type) {
    auto it = entries_.find(key);
    if (it != entries_.end() && (it->second.obj != obj || !IsValid(it->second.obj))) {
        // Target changed (e.g. the hero respawned): restore the old object if
        // it still exists and start over with the new one.
        if (IsValid(it->second.obj)) Restore(it->second);
        entries_.erase(it);
        it = entries_.end();
    }
    if (!enable) {
        if (it != entries_.end()) {
            Restore(it->second);
            entries_.erase(it);
        }
        return nullptr;
    }
    if (!obj) return nullptr;
    if (it == entries_.end()) {
        Entry n;
        n.obj = obj;
        n.index = IndexOf(obj);
        n.path = path;
        n.type = type;
        bool ok = false;
        switch (type) {
            case Type::Float: ok = GetFloat(obj, path, n.f); break;
            case Type::Int: ok = GetInt(obj, path, n.i); break;
            case Type::Bool: ok = GetBool(obj, path, n.b); break;
            case Type::Byte: ok = GetByte(obj, path, n.by); break;
            case Type::Raw: {
                PropRef r = Prop(obj, path);
                if (r) {
                    size_t sz = static_cast<size_t>(std::max(1, PropElementSize(r.prop)));
                    n.raw.assign(r.addr, r.addr + sz);
                    ok = true;
                }
                break;
            }
        }
        if (!ok) return nullptr;  // property missing in this build
        it = entries_.emplace(key, n).first;
    }
    return &it->second;
}

void OverrideStore::Restore(Entry& e) {
    if (!IsValid(e.obj)) return;
    switch (e.type) {
        case Type::Float: SetFloat(e.obj, e.path.c_str(), e.f); break;
        case Type::Int: SetInt(e.obj, e.path.c_str(), e.i); break;
        case Type::Bool: SetBool(e.obj, e.path.c_str(), e.b); break;
        case Type::Byte: SetByte(e.obj, e.path.c_str(), e.by); break;
        case Type::Raw: {
            PropRef r = Prop(e.obj, e.path.c_str());
            if (r && !e.raw.empty()) std::memcpy(r.addr, e.raw.data(), e.raw.size());
            break;
        }
    }
}

void OverrideStore::Float(const std::string& key, UObject* obj, const char* path, bool enable, float value) {
    if (Begin(key, obj, path, enable, Type::Float)) SetFloat(obj, path, value);
}
void OverrideStore::Int(const std::string& key, UObject* obj, const char* path, bool enable, int32_t value) {
    if (Begin(key, obj, path, enable, Type::Int)) SetInt(obj, path, value);
}
void OverrideStore::Bool(const std::string& key, UObject* obj, const char* path, bool enable, bool value) {
    if (Begin(key, obj, path, enable, Type::Bool)) SetBool(obj, path, value);
}
void OverrideStore::Byte(const std::string& key, UObject* obj, const char* path, bool enable, uint8_t value) {
    if (Begin(key, obj, path, enable, Type::Byte)) SetByte(obj, path, value);
}
void OverrideStore::Scale(const std::string& key, UObject* obj, const char* path, bool enable, float factor) {
    if (Entry* e = Begin(key, obj, path, enable, Type::Float)) SetFloat(obj, path, e->f * factor);
}

void OverrideStore::ScaleNumber(const std::string& key, UObject* obj, const char* path, bool enable, float factor) {
    PropRef r = obj ? Prop(obj, path) : PropRef{};
    const std::string& t = r ? PropType(r.prop) : std::string();
    if (t == "IntProperty") {
        if (Entry* e = Begin(key, obj, path, enable, Type::Int))
            SetInt(obj, path, static_cast<int32_t>(std::lround(e->i * factor)));
    } else if (t == "ByteProperty") {
        if (Entry* e = Begin(key, obj, path, enable, Type::Byte))
            SetByte(obj, path, static_cast<uint8_t>(std::min(255L, std::lround(e->by * factor))));
    } else if (t == "FloatProperty" || !enable) {
        if (Entry* e = Begin(key, obj, path, enable, Type::Float)) SetFloat(obj, path, e->f * factor);
    }
}

void OverrideStore::SetNumber(const std::string& key, UObject* obj, const char* path, bool enable, float value) {
    PropRef r = obj ? Prop(obj, path) : PropRef{};
    const std::string& t = r ? PropType(r.prop) : std::string();
    if (t == "IntProperty") Int(key, obj, path, enable, static_cast<int32_t>(std::lround(value)));
    else if (t == "ByteProperty") Byte(key, obj, path, enable, static_cast<uint8_t>(std::lround(value)));
    else Float(key, obj, path, enable, value);
}

void OverrideStore::Raw(const std::string& key, UObject* obj, const char* path, bool enable, const void* data,
                        size_t size) {
    if (Entry* e = Begin(key, obj, path, enable, Type::Raw)) {
        PropRef r = Prop(obj, path);
        if (r && size <= e->raw.size()) std::memcpy(r.addr, data, size);
    }
}

void OverrideStore::RestoreAll() {
    for (auto& kv : entries_) Restore(kv.second);
    entries_.clear();
}

void OverrideStore::Purge() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        const Entry& e = it->second;
        it = (!IsValid(e.obj) || IndexOf(e.obj) != e.index) ? entries_.erase(it) : std::next(it);
    }
}

}  // namespace omm::game
