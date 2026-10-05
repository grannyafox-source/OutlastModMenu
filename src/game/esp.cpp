// Collects ESP targets on the game thread. The render thread projects and
// draws them (ui/esp_draw.cpp) using the camera stored in the snapshot.
//
// Actor classes come from OLGame: OLBatteriesPickupFactory, OLCollectiblePickup
// (documents), OLGameplayItemPickup (keys, fuses...), OLRecordingMarker
// (camcorder notes), OLHidingSpot / OLBed, OLCheckpoint, OLDoor and every
// OLEnemyPawn subclass.
#include "enemies.h"
#include "features.h"
#include "outlast.h"

#include "../core/settings.h"
#include "../core/strutil.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace omm::game {

using namespace ue3;

namespace {
constexpr int kNone = -1;
constexpr int kLocker = 100;  // HidingSpot sub-kinds (labels only)
constexpr int kBed = 101;
constexpr size_t kMaxItems = 350;

struct Target {
    UObject* actor = nullptr;
    int32_t index = -1;
    int kind = kNone;  // EspCategory value, or kLocker / kBed
};

struct ClassRule {
    const char* className;
    int kind;
};
// Most specific classes first: the first rule a class derives from wins.
const ClassRule kRules[] = {
    {"OLEnemyPawn", static_cast<int>(EspCategory::Enemy)},
    {"OLBatteriesPickupFactory", static_cast<int>(EspCategory::Battery)},
    {"OLCollectiblePickup", static_cast<int>(EspCategory::Document)},
    {"OLGameplayItemPickup", static_cast<int>(EspCategory::KeyItem)},
    {"OLRecordingMarker", static_cast<int>(EspCategory::Recording)},
    {"OLHidingSpot", kLocker},
    {"OLBed", kBed},
    {"OLCheckpoint", static_cast<int>(EspCategory::Checkpoint)},
    {"OLDoor", static_cast<int>(EspCategory::Door)},
};

std::unordered_map<UClass*, int> g_classKind;  // cache: class -> kind
std::vector<Target> g_targets;
uint64_t g_lastScan = 0;
UObject* g_lastWorld = nullptr;

EspCategory CategoryOf(int kind) {
    if (kind == kLocker || kind == kBed) return EspCategory::HidingSpot;
    return static_cast<EspCategory>(kind);
}

int KindOfClass(UClass* cls) {
    auto it = g_classKind.find(cls);
    if (it != g_classKind.end()) return it->second;
    int kind = kNone;
    for (const ClassRule& r : kRules) {
        UClass* rc = FindClass(r.className);
        if (rc && IsChildOf(cls, rc)) {
            kind = r.kind;
            break;
        }
    }
    g_classKind[cls] = kind;
    return kind;
}

void Rescan(const ModState& s) {
    g_targets.clear();
    bool wanted[static_cast<int>(EspCategory::Count)] = {};
    bool any = false;
    for (int i = 0; i < static_cast<int>(EspCategory::Count); ++i) any |= (wanted[i] = s.esp[i].enabled);
    if (!any) return;
    // Classes appear as levels stream in; forget negative cache entries so
    // a class missing at the last scan gets another chance.
    for (auto it = g_classKind.begin(); it != g_classKind.end();)
        it = it->second == kNone ? g_classKind.erase(it) : std::next(it);
    UClass* actorClass = FindClass("Actor");
    if (!actorClass) return;
    ForEachObject([&](UObject* o) {
        UClass* cls = ClassOf(o);
        if (!cls) return true;
        int kind = KindOfClass(cls);
        if (kind == kNone || !wanted[static_cast<int>(CategoryOf(kind))]) return true;
        if (IsDefaultObject(o) || Bool(o, "bDeleteMe")) return true;
        // Placed actors have a WorldInfo; archetypes stored in packages do not.
        if (!Obj(o, "WorldInfo")) return true;
        g_targets.push_back({o, IndexOf(o), kind});
        return g_targets.size() < 8000;
    });
}

bool Contains(const std::vector<FName>& v, const FName& n) {
    return n.Index != 0 && std::find(v.begin(), v.end(), n) != v.end();
}

std::string PrettyName(const FName& n) {
    std::string s = NameToString(n);
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}
}  // namespace

void CollectEsp(const ModState& s, Snapshot& snap) {
    if (!s.espEnabled || !W().pc) {
        g_targets.clear();
        return;
    }
    uint64_t now = NowMs();
    if (W().worldInfo != g_lastWorld) {
        g_lastWorld = W().worldInfo;
        g_classKind.clear();
        g_lastScan = 0;
    }
    if (now - g_lastScan > 750) {
        g_lastScan = now;
        Rescan(s);
    }

    // Progress lists used for the "collected" state.
    std::vector<FName> documents, recordings;
    if (UObject* inv = Obj(W().pc, "InventoryManager")) GetNameArray(inv, "CollectedDocuments", documents);
    GetNameArray(W().pc, "CompletedRecordingMoments", recordings);

    const FVector eye = snap.camLocation;
    const float maxDist = s.espMaxDistance;
    std::vector<EspItem> items;
    items.reserve(g_targets.size());
    for (const Target& t : g_targets) {
        if (!IsValid(t.actor) || IndexOf(t.actor) != t.index || Bool(t.actor, "bDeleteMe")) continue;
        const EspCategory cat = CategoryOf(t.kind);
        if (!s.esp[static_cast<int>(cat)].enabled) continue;
        EspItem it;
        it.category = cat;
        if (!GetVector(t.actor, "Location", it.location)) continue;
        it.distance = (it.location - eye).Size();
        if (maxDist > 0.f && it.distance > maxDist) continue;
        bool hidden = Bool(t.actor, "bHidden");
        switch (cat) {
            case EspCategory::Enemy: {
                if (Int(t.actor, "Health", 1) <= 0 || hidden) continue;
                it.label = enemies::DisplayName(t.actor);
                it.height = Float(Obj(t.actor, "CylinderComponent"), "CollisionHeight", 90.f);
                UObject* bot = Obj(t.actor, "Controller");
                uint8_t st = 0;
                if (bot && GetByte(bot, "BehaviorState", st)) it.detail = enemies::StateName(st);
                break;
            }
            case EspCategory::Battery: {
                it.collected = hidden || Bool(t.actor, "bUsed") || Bool(t.actor, "bDisabled");
                int n = Int(t.actor, "NumBatteries", 1);
                it.label = n > 1 ? str::Format("Batteries x%d", n) : "Battery";
                break;
            }
            case EspCategory::Document: {
                FName name;
                GetName(t.actor, "CollectibleName", name);
                it.collected = hidden || Bool(t.actor, "bUsed") || Contains(documents, name);
                it.label = "Document";
                if (name.Index) it.detail = PrettyName(name);
                break;
            }
            case EspCategory::KeyItem: {
                FName name;
                GetName(t.actor, "ItemName", name);
                it.collected = hidden || Bool(t.actor, "bUsed") || Bool(t.actor, "bDisabled");
                it.label = name.Index ? PrettyName(name) : "Item";
                break;
            }
            case EspCategory::Recording: {
                FName moment;
                GetName(t.actor, "MomentName", moment);
                it.collected = Bool(t.actor, "bRecorded") || Contains(recordings, moment);
                if (!it.collected && !Bool(t.actor, "bEnabled", true)) it.detail = "not active yet";
                it.label = "Record here";
                if (moment.Index) it.detail = it.detail.empty() ? PrettyName(moment) : PrettyName(moment) + " - " + it.detail;
                break;
            }
            case EspCategory::HidingSpot:
                it.label = t.kind == kBed ? "Bed" : "Locker";
                break;
            case EspCategory::Checkpoint: {
                FName cp;
                GetName(t.actor, "CheckpointName", cp);
                it.label = "Checkpoint";
                if (cp.Index) it.detail = NameToString(cp);
                break;
            }
            case EspCategory::Door:
                if (hidden) continue;
                it.label = Bool(t.actor, "bLocked") ? "Locked door" : "Door";
                break;
            default:
                break;
        }
        if (it.collected && s.espHideCollected) continue;
        items.push_back(std::move(it));
    }
    if (items.size() > kMaxItems) {
        std::partial_sort(items.begin(), items.begin() + kMaxItems, items.end(),
                          [](const EspItem& a, const EspItem& b) { return a.distance < b.distance; });
        items.resize(kMaxItems);
    }
    snap.esp = std::move(items);
}

}  // namespace omm::game
