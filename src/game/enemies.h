// Enemy tracking, global AI modifiers, spawning and model swapping.
// Functions without "Ui" in the name run on the game thread.
#pragma once

#include "state.h"

#include <map>
#include <string>
#include <vector>

namespace omm::game::enemies {

// Asset paths are the ones the game's own levels use (they are only in
// memory while a level that contains them is loaded). When no enemy of the
// same class exists to copy from, the spawner falls back to these.
struct SpawnType {
    const char* label;
    const char* className;
    const char* behaviorTree;  // OLBTBehaviorTree path
    const char* voAsset;       // OLAIContextualVOAsset path (nullptr = none)
    const char* mesh;          // SkeletalMesh path (nullptr = class default)
    const char* meshHint;      // substring of the SkeletalMesh name, last resort
};
const std::vector<SpawnType>& SpawnTypes();

enum class Placement { InFront = 0, AtCrosshair, Behind, AroundPlayer };

struct SpawnRequest {
    int type = 0;            // index into SpawnTypes()
    int count = 1;
    int weapon = 0;          // EWeapon
    bool clone = false;      // copy an existing enemy (ActorTemplate)
    bool attack = true;
    Placement placement = Placement::InFront;
    float distance = 450.f;
};

const char* WeaponName(int weapon);
int WeaponCount();
const char* StateName(int behaviorState);
std::string DisplayName(void* enemyPawn);

// Game thread.
void Refresh(bool force);
void Apply(const ModState& s, float dt);
void FillSnapshot(Snapshot& snap);
bool Spawn(const SpawnRequest& r);
void Kill(int32_t index);
void KillAll(bool onlySpawned);
void SetFrozen(int32_t index, bool frozen);
void SetIgnorePlayer(int32_t index, bool ignore);
void SetWeapon(int32_t index, int weapon);
void TeleportTo(int32_t index);
void BringToPlayer(int32_t index);

// Models -----------------------------------------------------------------
struct MeshEntry {
    std::string path;  // object path, e.g. 02_Soldier.Pawn.Soldier-03
    std::string name;
};
// "AllEnemies", "Hero", or an enemy class name such as "OLEnemySoldier".
using ModelRules = std::map<std::string, std::string>;

void RequestMeshList();              // UI: refresh on next frame
std::vector<MeshEntry> MeshList();   // UI: last result
ModelRules GetModelRules();
void SetModelRule(const std::string& target, const std::string& meshPath);
void RemoveModelRule(const std::string& target);
void ClearModelRules();
void SaveModelRules();
void LoadModelRules();
bool SwapMesh(int32_t index, const std::string& meshPath);  // game thread
bool SwapHeroMesh(const std::string& meshPath);             // game thread
void RandomizeAllModels();                                  // game thread

struct ModelPackSwap {
    std::string target;
    std::string mesh;
    float scale = 1.f;
};
struct ModelPack {
    std::string file;
    std::string name;
    std::string author;
    std::string description;
    std::vector<ModelPackSwap> swaps;
};
std::vector<ModelPack> ScanModelPacks();
void ActivatePack(const ModelPack& pack);  // game thread: loads meshes, adds rules

}  // namespace omm::game::enemies
