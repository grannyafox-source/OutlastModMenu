#include "enemies.h"

#include "../core/fileutil.h"
#include "../core/ini.h"
#include "../core/log.h"
#include "../core/paths.h"
#include "../core/settings.h"
#include "../core/strutil.h"
#include "../core/sync.h"
#include "../ue3/call.h"
#include "outlast.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace omm::game::enemies {

using namespace ue3;

namespace {
struct Tracked {
    UObject* pawn = nullptr;
    int32_t index = -1;
    bool spawned = false;
};

std::vector<Tracked> g_list;
uint64_t g_lastRefresh = 0;
std::map<int32_t, UObject*> g_spawned;       // index -> pawn spawned by the mod
std::map<int32_t, bool> g_manualFrozen;
std::set<int32_t> g_removed;  // enemies that could not be destroyed: kept hidden and frozen
std::map<int32_t, bool> g_hiddenApplied;  // index -> was it hidden before the mod hid it
std::map<int32_t, FVector> g_scaleOriginal;
std::map<int32_t, UObject*> g_originalMesh;  // first mesh seen before a swap
UObject* g_heroOriginalMesh = nullptr;
UObject* g_heroMeshOwner = nullptr;
float g_hordeTimer = 0.f;

Mutex g_rulesLock;
ModelRules g_rules;
std::map<std::string, UObject*> g_meshCache;
std::map<std::string, uint64_t> g_meshMiss;

Mutex g_meshListLock;
std::vector<MeshEntry> g_meshList;
volatile bool g_meshListRequested = false;

const char* const kWeapons[] = {"None",   "Knife",      "Butcher knife", "Bone shears",   "Machete",
                                "Nightstick", "Pipe", "Wood plank",    "Cannibal drill"};

const char* const kSpeedPaths[] = {
    "NormalSpeedValues.PatrolSpeed",      "NormalSpeedValues.InvestigateSpeed",      "NormalSpeedValues.ChaseSpeed",
    "DarknessSpeedValues.PatrolSpeed",    "DarknessSpeedValues.InvestigateSpeed",    "DarknessSpeedValues.ChaseSpeed",
    "ElectricitySpeedValues.PatrolSpeed", "ElectricitySpeedValues.InvestigateSpeed", "ElectricitySpeedValues.ChaseSpeed",
};
const char* const kDamagePaths[] = {"AttackNormalDamage", "AttackThrowDamage", "DoorBashDamage", "VaultDamage"};

bool Alive(const Tracked& t) {
    return IsValid(t.pawn) && IndexOf(t.pawn) == t.index && !Bool(t.pawn, "bDeleteMe");
}

Tracked* FindTracked(int32_t index) {
    for (Tracked& t : g_list)
        if (t.index == index && Alive(t)) return &t;
    return nullptr;
}

// "02_AI_Behaviors.Soldier_BT" -> "Soldier_BT"
std::string ShortName(const char* path) {
    std::string p = path ? path : "";
    size_t dot = p.rfind('.');
    return dot == std::string::npos ? p : p.substr(dot + 1);
}

float Frand() { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); }

int32_t YawTowards(const FVector& from, const FVector& to) {
    return static_cast<int32_t>(std::atan2(to.Y - from.Y, to.X - from.X) * kRadToRot);
}

UObject* ResolveMesh(const std::string& path) {
    auto it = g_meshCache.find(path);
    if (it != g_meshCache.end() && IsValid(it->second)) return it->second;
    uint64_t now = NowMs();
    auto miss = g_meshMiss.find(path);
    if (miss != g_meshMiss.end() && now - miss->second < 5000) return nullptr;
    UObject* m = LoadObjectByPath(path, "SkeletalMesh");
    if (m) {
        g_meshCache[path] = m;
        g_meshMiss.erase(path);
    } else {
        g_meshMiss[path] = now;
    }
    return m;
}

std::string RuleFor(UObject* pawn) {
    // Copy first: game memory is read below, and no lock may be held while
    // doing that (see core/guard.h).
    ModelRules rules;
    {
        LockGuard lock(g_rulesLock);
        if (g_rules.empty()) return std::string();
        rules = g_rules;
    }
    for (UStruct* c = ClassOf(pawn); c; c = SuperOf(c)) {
        auto it = rules.find(Name(c));
        if (it != rules.end()) return it->second;
    }
    auto all = rules.find("AllEnemies");
    return all != rules.end() ? all->second : std::string();
}

void ApplyMeshRule(const Tracked& t) {
    std::string rule = RuleFor(t.pawn);
    UObject* comp = Obj(t.pawn, "Mesh");
    if (!comp) return;
    UObject* current = Obj(comp, "SkeletalMesh");
    if (rule.empty()) {
        auto it = g_originalMesh.find(t.index);
        if (it != g_originalMesh.end()) {
            if (IsValid(it->second) && current != it->second) SetSkeletalMesh(comp, it->second);
            g_originalMesh.erase(it);
        }
        return;
    }
    UObject* mesh = ResolveMesh(rule);
    if (!mesh || current == mesh) return;
    if (!g_originalMesh.count(t.index)) g_originalMesh[t.index] = current;
    SetSkeletalMesh(comp, mesh);
}

void ApplyHeroRule() {
    UObject* hero = W().hero;
    if (!hero) return;
    std::string rule;
    {
        LockGuard lock(g_rulesLock);
        auto it = g_rules.find("Hero");
        if (it != g_rules.end()) rule = it->second;
    }
    UObject* comp = Obj(hero, "Mesh");
    UObject* shadow = Obj(hero, "ShadowProxy");
    if (!comp) return;
    if (g_heroMeshOwner != hero) {
        g_heroMeshOwner = hero;
        g_heroOriginalMesh = nullptr;
    }
    UObject* current = Obj(comp, "SkeletalMesh");
    if (rule.empty()) {
        if (g_heroOriginalMesh && IsValid(g_heroOriginalMesh) && current != g_heroOriginalMesh) {
            SetSkeletalMesh(comp, g_heroOriginalMesh);
            if (shadow) SetSkeletalMesh(shadow, g_heroOriginalMesh);
        }
        g_heroOriginalMesh = nullptr;
        return;
    }
    UObject* mesh = ResolveMesh(rule);
    if (!mesh || current == mesh) return;
    if (!g_heroOriginalMesh) g_heroOriginalMesh = current;
    SetSkeletalMesh(comp, mesh);
    if (shadow) SetSkeletalMesh(shadow, mesh);
}

void SetHidden(UObject* pawn, bool hidden) {
    Call c(pawn, "SetHidden");
    if (c.Ok()) {
        c.Bool("bNewHidden", hidden);
        c.Invoke();
    } else {
        SetBool(pawn, "bHidden", hidden);
    }
}

// Hides an enemy, remembering whether the game had it hidden already (some
// enemies wait hidden for a scripted entrance) so that showing it again only
// undoes what the mod did.
void SetHiddenIfNeeded(const Tracked& t, bool hidden) {
    auto it = g_hiddenApplied.find(t.index);
    if (hidden) {
        if (it != g_hiddenApplied.end()) return;
        bool wasHidden = Bool(t.pawn, "bHidden");
        g_hiddenApplied[t.index] = wasHidden;
        if (!wasHidden) SetHidden(t.pawn, true);
    } else if (it != g_hiddenApplied.end()) {
        bool wasHidden = it->second;
        g_hiddenApplied.erase(it);
        if (!wasHidden) SetHidden(t.pawn, false);
    }
}

void SetScaleIfNeeded(const Tracked& t, bool enable, const FVector& scale) {
    auto it = g_scaleOriginal.find(t.index);
    if (!enable) {
        if (it != g_scaleOriginal.end()) {
            Call c(t.pawn, "SetDrawScale3D");
            if (c.Ok()) c.Vector("NewScale3D", it->second).Invoke();
            g_scaleOriginal.erase(it);
        }
        return;
    }
    FVector current;
    if (!GetVector(t.pawn, "DrawScale3D", current)) return;
    if (it == g_scaleOriginal.end()) g_scaleOriginal[t.index] = current;
    if (std::fabs(current.X - scale.X) < 0.001f && std::fabs(current.Y - scale.Y) < 0.001f &&
        std::fabs(current.Z - scale.Z) < 0.001f)
        return;
    Call c(t.pawn, "SetDrawScale3D");
    if (c.Ok()) c.Vector("NewScale3D", scale).Invoke();
}

bool ApplyModifiersStruct(UObject* pawn) {
    PropRef mods = Prop(pawn, "Modifiers");
    if (!mods) return false;
    Call am(pawn, "ApplyModifiers");
    if (!am.Ok()) return false;
    am.Raw("NewModifiers", mods.addr, static_cast<size_t>(std::max(1, PropElementSize(mods.prop))));
    return am.Invoke();
}

UObject* FindTemplate(UClass* cls) {
    UObject* best = nullptr;
    for (const Tracked& t : g_list) {
        if (!Alive(t) || !IsA(t.pawn, cls)) continue;
        if (ClassOf(t.pawn) == cls) return t.pawn;  // exact class preferred
        if (!best) best = t.pawn;
    }
    return best;
}

UObject* FindMeshByHint(const char* hint) {
    if (!hint) return nullptr;
    UObject* found = nullptr;
    UClass* meshClass = FindClass("SkeletalMesh");
    if (!meshClass) return nullptr;
    ForEachObject([&](UObject* o) {
        if (ClassOf(o) == meshClass && !IsDefaultObject(o) && str::IContains(Name(o), hint)) {
            found = o;
            return false;
        }
        return true;
    });
    return found;
}

UObject* SpawnActor(UClass* cls, const FVector& loc, const FRotator& rot, UObject* tmpl, bool noCollisionFail) {
    UObject* pc = W().pc;
    if (!pc || !cls) return nullptr;
    Call sp(pc, "Spawn");
    if (!sp.Ok()) return nullptr;
    sp.Obj("SpawnClass", cls).Vector("SpawnLocation", loc).Rotator("SpawnRotation", rot);
    FName tag;
    if (FindName("CustomEnemy", tag)) sp.NameP("SpawnTag", tag);
    if (tmpl) sp.Obj("ActorTemplate", tmpl);
    if (sp.HasParam("bNoCollisionFail")) sp.Bool("bNoCollisionFail", noCollisionFail);
    if (!sp.Invoke()) return nullptr;
    UObject* a = sp.RetObj();
    return IsValid(a) ? a : nullptr;
}

bool DestroyActor(UObject* a) {
    if (!a) return false;
    Call d(a, "Destroy");
    return d.Ok() && d.Invoke() && (Bool(a, "bDeleteMe") || !IsValid(a));
}
}  // namespace

const std::vector<SpawnType>& SpawnTypes() {
    static const std::vector<SpawnType> types = {
        {"Chris Walker", "OLEnemySoldier", "02_AI_Behaviors.Soldier_BT", "02_AI_Behaviors.Soldier", nullptr,
         "Soldier"},
        {"Patient (variant)", "OLEnemyGenericPatient", "02_AI_Behaviors.Generic_FullLoop_BT", nullptr, nullptr,
         nullptr},
        {"Dr. Richard Trager", "OLEnemySurgeon", "02_AI_Behaviors.Surgeon_FullLoop_BT", "02_AI_Behaviors.Surgeon",
         "02_Surgeon.Mesh.Surgeon", "Surgeon"},
        {"The Walrider", "OLEnemyNanoCloud", "02_AI_Behaviors.NanoCloud_BT", nullptr, nullptr, "Nano"},
        {"Eddie Gluskin (the Groom)", "OLEnemyGroom", "02_AI_Behaviors.Generic_FullLoop_BT",
         "02_AI_Behaviors.Groom_soft", "02_Groom.Groom_Shirt", "Groom"},
        {"Frank Manera (the Cannibal)", "OLEnemyCannibal", "02_AI_Behaviors.Surgeon_FullLoop_BT",
         "02_AI_Behaviors.Cannibal", nullptr, "Cannibal"},
        {"Father Martin (as an enemy)", "OLEnemyGenericPatient", "02_AI_Behaviors.Generic_FullLoop_BT", nullptr,
         "02_Priest.Pawn.Priest-01", "Priest"},
    };
    return types;
}

const char* WeaponName(int weapon) {
    return weapon >= 0 && weapon < WeaponCount() ? kWeapons[weapon] : "?";
}
int WeaponCount() { return static_cast<int>(sizeof(kWeapons) / sizeof(kWeapons[0])); }

const char* StateName(int s) {
    switch (s) {
        case 0: return "Idle";
        case 1: return "Patrolling";
        case 2: return "Investigating";
        case 3: return "Chasing";
        default: return "?";
    }
}

std::string DisplayName(void* p) {
    auto* pawn = static_cast<UObject*>(p);
    if (UObject* mesh = Obj(Obj(pawn, "Mesh"), "SkeletalMesh"))
        if (str::IContains(Name(mesh), "Priest")) return "Father Martin";
    if (IsA(pawn, "OLEnemySoldier") && !IsA(pawn, "OLEnemyGroom")) return "Chris Walker";
    if (IsA(pawn, "OLEnemyGroom")) return "Eddie Gluskin";
    if (IsA(pawn, "OLEnemySurgeon")) return "Dr. Trager";
    if (IsA(pawn, "OLEnemyCannibal")) return "Frank Manera";
    if (IsA(pawn, "OLEnemyNanoCloud")) return "The Walrider";
    if (IsA(pawn, "OLEnemyGenericPatient")) return "Patient";
    return Name(ClassOf(pawn));
}

void Refresh(bool force) {
    uint64_t now = NowMs();
    if (!force && now - g_lastRefresh < 500) return;
    g_lastRefresh = now;
    std::vector<Tracked> list;
    if (UClass* base = FindClass("OLEnemyPawn")) {
        for (UObject* o : FindInstances(base, 512)) {
            // Archetypes (templates stored in packages) are not in the world:
            // they have no WorldInfo and must never be modified or destroyed.
            if (Bool(o, "bDeleteMe") || !Obj(o, "WorldInfo")) continue;
            Tracked t;
            t.pawn = o;
            t.index = IndexOf(o);
            auto sp = g_spawned.find(t.index);
            t.spawned = sp != g_spawned.end() && sp->second == o;
            list.push_back(t);
        }
    }
    g_list.swap(list);
    for (auto it = g_spawned.begin(); it != g_spawned.end();)
        it = (!IsValid(it->second) || IndexOf(it->second) != it->first) ? g_spawned.erase(it) : std::next(it);
    // Forget per-enemy state of enemies that no longer exist (indices get reused).
    std::set<int32_t> alive;
    for (const Tracked& t : g_list) alive.insert(t.index);
    auto prune = [&](auto& m) {
        for (auto it = m.begin(); it != m.end();) it = alive.count(it->first) ? std::next(it) : m.erase(it);
    };
    prune(g_manualFrozen);
    prune(g_hiddenApplied);
    prune(g_scaleOriginal);
    prune(g_originalMesh);
    for (auto it = g_removed.begin(); it != g_removed.end();) it = alive.count(*it) ? std::next(it) : g_removed.erase(it);
}

void Apply(const ModState& s, float dt) {
    Refresh(false);
    if (g_meshListRequested) {
        g_meshListRequested = false;
        std::vector<MeshEntry> meshes;
        if (UClass* meshClass = FindClass("SkeletalMesh")) {
            ForEachObject([&](UObject* o) {
                if (ClassOf(o) == meshClass && !IsDefaultObject(o)) meshes.push_back({PathName(o), Name(o)});
                return meshes.size() < 4000;
            });
        }
        std::sort(meshes.begin(), meshes.end(), [](const MeshEntry& a, const MeshEntry& b) { return a.name < b.name; });
        LockGuard lock(g_meshListLock);
        g_meshList.swap(meshes);
    }

    OverrideStore& o = Overrides();
    const FVector scale(s.enemyScale[0], s.enemyScale[1], s.enemyScale[2]);
    for (const Tracked& t : g_list) {
        if (!Alive(t)) continue;
        UObject* e = t.pawn;
        UObject* bot = Obj(e, "Controller");
        const std::string k = "e" + std::to_string(t.index) + ":";
        const bool removed = g_removed.count(t.index) != 0;
        bool frozen = removed || s.freezeEnemies || (g_manualFrozen.count(t.index) && g_manualFrozen[t.index]);
        bool dilate = frozen || s.enemyTimeScaleOverride;
        float td = frozen ? 0.0001f : s.enemyTimeScale;
        o.Float(k + "td", e, "CustomTimeDilation", dilate, td);
        o.Float(k + "tdBot", bot, "CustomTimeDilation", dilate, td);
        o.Bool(k + "attack", e, "Modifiers.bShouldAttack", s.passiveEnemies, false);
        // "Invisible to enemies" also blinds and deafens them, in case the
        // player's own ghost flag is not enough in some situation.
        o.Bool(k + "blind", Obj(bot, "SightComponent"), "bIgnoreTarget", s.blindEnemies || s.invisible, true);
        o.Float(k + "deaf", e, "HearingThreshold", s.deafEnemies || s.invisible, 0.f);
        for (const char* p : kSpeedPaths) o.Scale(k + p, e, p, s.enemySpeedOverride, s.enemySpeedMultiplier);
        for (const char* p : kDamagePaths) o.ScaleNumber(k + p, e, p, s.enemyDamageOverride, s.enemyDamageMultiplier);
        SetHiddenIfNeeded(t, s.invisibleEnemies || removed);
        SetScaleIfNeeded(t, s.enemyScaleOverride, scale);
        if (s.autoApplyModelSwaps) ApplyMeshRule(t);
    }
    if (s.autoApplyModelSwaps) ApplyHeroRule();

    // Horde mode: keep bringing in more enemies, like the Ultimate Bendy mod.
    if (s.hordeMode && W().hero) {
        g_hordeTimer += dt;
        if (g_hordeTimer >= std::max(5.f, s.hordeInterval)) {
            g_hordeTimer = 0.f;
            if (static_cast<int>(g_spawned.size()) < s.hordeMaxEnemies) {
                SpawnRequest r;
                int n = static_cast<int>(SpawnTypes().size());
                r.type = s.hordeEnemyType >= 0 && s.hordeEnemyType < n ? s.hordeEnemyType : std::rand() % n;
                r.placement = Placement::AroundPlayer;
                r.distance = 900.f + Frand() * 600.f;
                if (Spawn(r)) Notify("Something else is here...", 3.f);
            }
        }
    } else {
        g_hordeTimer = 0.f;
    }
}

void FillSnapshot(Snapshot& snap) {
    FVector heroLoc = snap.location;
    for (const Tracked& t : g_list) {
        if (!Alive(t) || g_removed.count(t.index)) continue;
        EnemyInfo info;
        info.index = t.index;
        info.className = Name(ClassOf(t.pawn));
        info.displayName = DisplayName(t.pawn);
        UObject* bot = Obj(t.pawn, "Controller");
        uint8_t st = 0;
        if (bot && GetByte(bot, "BehaviorState", st)) info.state = StateName(st);
        UObject* mesh = Obj(Obj(t.pawn, "Mesh"), "SkeletalMesh");
        info.mesh = mesh ? Name(mesh) : std::string();
        GetVector(t.pawn, "Location", info.location);
        info.distance = (info.location - heroLoc).Size();
        info.health = Int(t.pawn, "Health");
        info.frozen = g_manualFrozen.count(t.index) && g_manualFrozen[t.index];
        info.passive = !Bool(t.pawn, "Modifiers.bShouldAttack", true);
        info.spawnedByMod = t.spawned;
        snap.enemies.push_back(info);
    }
    std::sort(snap.enemies.begin(), snap.enemies.end(),
              [](const EnemyInfo& a, const EnemyInfo& b) { return a.distance < b.distance; });
    snap.modSpawnedEnemies = static_cast<int>(g_spawned.size());
}

bool Spawn(const SpawnRequest& r) {
    const World& w = W();
    if (!w.pc || !w.hero || r.type < 0 || r.type >= static_cast<int>(SpawnTypes().size())) return false;
    const SpawnType& type = SpawnTypes()[r.type];
    UClass* cls = FindClass(type.className);
    if (!cls) {
        Notify(std::string(type.label) + " is not available in this game/version.");
        return false;
    }
    UClass* botClass = FindClass("OLBot");
    if (!botClass) {
        Notify("OLBot class not found - cannot spawn enemies.");
        return false;
    }
    Refresh(true);
    UObject* tmpl = FindTemplate(cls);
    FVector heroLoc;
    GetVector(w.hero, "Location", heroLoc);
    FVector camLoc;
    FRotator camRot;
    float fov;
    if (!GetViewPoint(camLoc, camRot, fov)) GetRotator(w.hero, "Rotation", camRot);
    FRotator flat{0, camRot.Yaw, 0};
    FVector fwd = RotatorToVector(flat);

    int spawnedCount = 0;
    for (int i = 0; i < std::max(1, r.count); ++i) {
        FVector base = heroLoc;
        switch (r.placement) {
            case Placement::InFront: base = heroLoc + fwd * r.distance; break;
            case Placement::Behind: base = heroLoc - fwd * r.distance; break;
            case Placement::AtCrosshair: {
                FVector hit;
                if (TraceFromCamera(8000.f, hit)) base = hit;
                else base = heroLoc + fwd * r.distance;
                break;
            }
            case Placement::AroundPlayer: {
                float a = Frand() * 6.2831853f;
                base = heroLoc + FVector(std::cos(a), std::sin(a), 0) * r.distance;
                break;
            }
        }
        base.Z = heroLoc.Z + 20.f;
        // Spread a group out a little.
        if (i > 0) base = base + FVector((Frand() - 0.5f) * 160.f, (Frand() - 0.5f) * 160.f, 0);
        FRotator face{0, YawTowards(base, heroLoc), 0};
        UObject* tmplArg = r.clone ? tmpl : nullptr;
        UObject* pawn = nullptr;
        // Try a few positions without overlapping geometry, then force it.
        const FVector nudges[] = {FVector(0, 0, 0), FVector(80, 0, 40), FVector(-80, 0, 40), FVector(0, 80, 40),
                                  FVector(0, -80, 40)};
        for (const FVector& n : nudges) {
            pawn = SpawnActor(cls, base + n, face, tmplArg, false);
            if (pawn) break;
        }
        if (!pawn) pawn = SpawnActor(cls, base, face, tmplArg, true);
        if (!pawn) {
            LOGW("Spawn of %s failed", type.className);
            continue;
        }
        UObject* bot = SpawnActor(botClass, base, face, nullptr, true);
        if (!bot) {
            DestroyActor(pawn);
            continue;
        }
        Call possess(bot, "Possess");
        if (possess.Ok()) {
            if (possess.HasParam("aPawn")) possess.Obj("aPawn", pawn);
            else possess.Obj("inPawn", pawn);
            if (possess.HasParam("bVehicleTransition")) possess.Bool("bVehicleTransition", false);
            possess.Invoke();
        }
        // Give the new enemy the brains and looks of one already in the level,
        // or fall back to assets that are loaded in memory.
        if (tmpl && !r.clone) {
            if (UObject* bt = Obj(tmpl, "BehaviorTree")) SetObj(pawn, "BehaviorTree", bt);
            if (UObject* vo = Obj(tmpl, "VOAsset")) SetObj(pawn, "VOAsset", vo);
            UObject* tmplMesh = Obj(Obj(tmpl, "Mesh"), "SkeletalMesh");
            UObject* comp = Obj(pawn, "Mesh");
            if (!type.mesh && tmplMesh && comp && Obj(comp, "SkeletalMesh") != tmplMesh) SetSkeletalMesh(comp, tmplMesh);
        } else if (!tmpl) {
            if (!Obj(pawn, "BehaviorTree")) {
                UObject* bt = LoadObjectByPath(type.behaviorTree, "OLBTBehaviorTree");
                if (!bt) bt = FindObject(ShortName(type.behaviorTree).c_str(), "OLBTBehaviorTree");
                if (bt) SetObj(pawn, "BehaviorTree", bt);
                else Notify("No AI behaviour for this enemy is loaded here - it may stand still.", 4.f);
            }
            if (type.voAsset && !Obj(pawn, "VOAsset")) {
                if (UObject* vo = LoadObjectByPath(type.voAsset, "OLAIContextualVOAsset")) SetObj(pawn, "VOAsset", vo);
            }
        }
        // Characters that share a class (Trager, the Groom, Father Martin...)
        // differ only by their mesh.
        if (!r.clone && (type.mesh || (!tmpl && type.meshHint))) {
            UObject* mesh = type.mesh ? LoadObjectByPath(type.mesh, "SkeletalMesh") : nullptr;
            if (!mesh) mesh = FindMeshByHint(type.meshHint);
            UObject* comp = Obj(pawn, "Mesh");
            if (mesh && comp && Obj(comp, "SkeletalMesh") != mesh) SetSkeletalMesh(comp, mesh);
        }
        if (UObject* comp = Obj(pawn, "Mesh")) {
            if (!Obj(comp, "SkeletalMesh")) {
                UObject* fallback = tmpl ? Obj(Obj(tmpl, "Mesh"), "SkeletalMesh") : nullptr;
                if (fallback) SetSkeletalMesh(comp, fallback);
                else Notify(std::string(type.label) + ": model not loaded in this level - it may be invisible.", 4.f);
            }
        }
        SetBool(pawn, "Modifiers.bShouldAttack", r.attack);
        SetBool(pawn, "Modifiers.bUseForMusic", true);
        SetByte(pawn, "Modifiers.WeaponToUse", static_cast<uint8_t>(r.weapon));
        ApplyModifiersStruct(pawn);
        CallNoArgs(pawn, "InitContextualVO");
        g_spawned[IndexOf(pawn)] = pawn;
        ++spawnedCount;
        LOGI("Spawned %s at (%.0f, %.0f, %.0f)%s", type.className, base.X, base.Y, base.Z,
             tmpl ? " using an existing enemy as template" : "");
    }
    Refresh(true);
    return spawnedCount > 0;
}

void Kill(int32_t index) {
    Tracked* t = FindTracked(index);
    if (!t) return;
    UObject* bot = Obj(t->pawn, "Controller");
    bool ok = DestroyActor(t->pawn);
    if (bot) DestroyActor(bot);
    if (!ok) {
        // Destroy unavailable: make it harmless, frozen and invisible instead.
        g_removed.insert(index);
        SetHiddenIfNeeded(*t, true);
        SetBool(t->pawn, "Modifiers.bShouldAttack", false);
    }
    g_spawned.erase(index);
    Refresh(true);
}

void KillAll(bool onlySpawned) {
    Refresh(true);
    std::vector<int32_t> ids;
    for (const Tracked& t : g_list)
        if (!onlySpawned || t.spawned) ids.push_back(t.index);
    for (int32_t id : ids) Kill(id);
}

void SetFrozen(int32_t index, bool frozen) { g_manualFrozen[index] = frozen; }

void SetIgnorePlayer(int32_t index, bool ignore) {
    Tracked* t = FindTracked(index);
    if (!t) return;
    UObject* bot = Obj(t->pawn, "Controller");
    Call c(bot, "ToggleAIIgnorePlayer");
    if (c.Ok()) c.Bool("bEnable", ignore).Invoke();
    SetBool(t->pawn, "Modifiers.bShouldAttack", !ignore);
    ApplyModifiersStruct(t->pawn);
}

void SetWeapon(int32_t index, int weapon) {
    Tracked* t = FindTracked(index);
    if (!t || weapon < 0 || weapon >= WeaponCount()) return;
    SetByte(t->pawn, "Modifiers.WeaponToUse", static_cast<uint8_t>(weapon));
    ApplyModifiersStruct(t->pawn);
}

void TeleportTo(int32_t index) {
    Tracked* t = FindTracked(index);
    if (!t) return;
    FVector loc;
    FRotator rot;
    GetVector(t->pawn, "Location", loc);
    GetRotator(t->pawn, "Rotation", rot);
    FVector back = RotatorToVector(FRotator{0, rot.Yaw, 0}) * -200.f;
    FRotator face{0, rot.Yaw, 0};
    TeleportPlayer(loc + back + FVector(0, 0, 40), &face);
}

void BringToPlayer(int32_t index) {
    Tracked* t = FindTracked(index);
    UObject* hero = W().hero;
    if (!t || !hero) return;
    FVector heroLoc;
    FRotator heroRot;
    GetVector(hero, "Location", heroLoc);
    GetRotator(hero, "Rotation", heroRot);
    FVector dest = heroLoc + RotatorToVector(FRotator{0, heroRot.Yaw, 0}) * 300.f + FVector(0, 0, 20);
    Call c(t->pawn, "SetLocation");
    if (c.Ok()) c.Vector("NewLocation", dest).Invoke();
}

// --- Models ---------------------------------------------------------------

void RequestMeshList() { g_meshListRequested = true; }

std::vector<MeshEntry> MeshList() {
    LockGuard lock(g_meshListLock);
    return g_meshList;
}

ModelRules GetModelRules() {
    LockGuard lock(g_rulesLock);
    return g_rules;
}

void SetModelRule(const std::string& target, const std::string& meshPath) {
    {
        LockGuard lock(g_rulesLock);
        g_rules[target] = meshPath;
    }
    SaveModelRules();
}

void RemoveModelRule(const std::string& target) {
    {
        LockGuard lock(g_rulesLock);
        g_rules.erase(target);
    }
    SaveModelRules();
}

void ClearModelRules() {
    {
        LockGuard lock(g_rulesLock);
        g_rules.clear();
    }
    SaveModelRules();
}

void SaveModelRules() {
    ModelRules copy = GetModelRules();
    Settings& cfg = Settings::Get();
    for (const auto& kv : cfg.GetSection("ModelSwaps")) cfg.RemoveKey("ModelSwaps", kv.first.c_str());
    for (const auto& kv : copy) cfg.SetString("ModelSwaps", kv.first.c_str(), kv.second);
}

void LoadModelRules() {
    ModelRules rules;
    for (const auto& kv : Settings::Get().GetSection("ModelSwaps"))
        if (!kv.second.empty()) rules[kv.first] = kv.second;
    LockGuard lock(g_rulesLock);
    g_rules = rules;
}

bool SwapMesh(int32_t index, const std::string& meshPath) {
    Tracked* t = FindTracked(index);
    if (!t) return false;
    UObject* mesh = ResolveMesh(meshPath);
    UObject* comp = Obj(t->pawn, "Mesh");
    if (!mesh || !comp) return false;
    if (!g_originalMesh.count(index)) g_originalMesh[index] = Obj(comp, "SkeletalMesh");
    return SetSkeletalMesh(comp, mesh);
}

bool SwapHeroMesh(const std::string& meshPath) {
    UObject* hero = W().hero;
    UObject* mesh = ResolveMesh(meshPath);
    if (!hero || !mesh) return false;
    bool ok = SetSkeletalMesh(Obj(hero, "Mesh"), mesh);
    if (UObject* shadow = Obj(hero, "ShadowProxy")) SetSkeletalMesh(shadow, mesh);
    return ok;
}

void RandomizeAllModels() {
    UClass* meshClass = FindClass("SkeletalMesh");
    if (!meshClass) return;
    // Character meshes only: those currently used by some enemy, plus any
    // mesh whose name hints at a character.
    std::vector<UObject*> pool;
    std::set<UObject*> seen;
    for (const Tracked& t : g_list) {
        if (!Alive(t)) continue;
        if (UObject* m = Obj(Obj(t.pawn, "Mesh"), "SkeletalMesh")) {
            if (seen.insert(m).second) pool.push_back(m);
        }
    }
    if (pool.size() < 2) {
        Notify("Not enough different character models are loaded here.");
        return;
    }
    for (const Tracked& t : g_list) {
        if (!Alive(t)) continue;
        UObject* comp = Obj(t.pawn, "Mesh");
        if (!comp) continue;
        if (!g_originalMesh.count(t.index)) g_originalMesh[t.index] = Obj(comp, "SkeletalMesh");
        SetSkeletalMesh(comp, pool[static_cast<size_t>(std::rand()) % pool.size()]);
    }
}

std::vector<ModelPack> ScanModelPacks() {
    std::vector<ModelPack> packs;
    std::string dir = paths::ModSubdir("models");
    for (const fs::DirEntry& e : fs::List(dir)) {
        if (e.isDir || fs::Extension(e.name) != ".ini") continue;
        IniDocument doc;
        if (!doc.Load(e.path)) continue;
        ModelPack p;
        p.file = e.name;
        p.name = doc.Get("Pack", "Name").value_or(fs::StripExtension(e.name));
        p.author = doc.Get("Pack", "Author").value_or("");
        p.description = doc.Get("Pack", "Description").value_or("");
        for (const std::string& sec : doc.Sections()) {
            if (!str::IStartsWith(sec, "Swap")) continue;
            ModelPackSwap s;
            s.target = doc.Get(sec, "Target").value_or("");
            s.mesh = doc.Get(sec, "Mesh").value_or("");
            str::ParseFloat(doc.Get(sec, "Scale").value_or("1"), s.scale);
            if (!s.target.empty() && !s.mesh.empty()) p.swaps.push_back(s);
        }
        packs.push_back(p);
    }
    return packs;
}

void ActivatePack(const ModelPack& pack) {
    int ok = 0;
    for (const ModelPackSwap& s : pack.swaps) {
        UObject* mesh = ResolveMesh(s.mesh);
        if (!mesh) {
            Notify("Could not load " + s.mesh + " (is the package installed?)", 4.f);
            continue;
        }
        SetModelRule(s.target, PathName(mesh));
        ++ok;
    }
    if (ok) Notify(str::Format("Model pack '%s': %d swap(s) active", pack.name.c_str(), ok));
}

}  // namespace omm::game::enemies
