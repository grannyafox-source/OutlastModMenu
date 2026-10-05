// Access to Outlast's game objects and the basic actions every feature uses.
// Game thread only.
//
// Class and property names come from the game's own config files
// (OLGame.ini, OLEnemy.ini) and its UnrealScript classes (OLHero,
// OLPlayerController, OLCheatManager, OLEnemyPawn, OLBot...).
#pragma once

#include "../ue3/engine.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace omm::game {

using ue3::FName;
using ue3::FRotator;
using ue3::FVector;
using ue3::UObject;

struct World {
    UObject* engine = nullptr;       // OLEngine (GEngine)
    UObject* viewport = nullptr;     // OLGameViewportClient
    UObject* localPlayer = nullptr;  // LocalPlayer
    UObject* pc = nullptr;           // OLPlayerController
    UObject* hero = nullptr;         // OLHero
    UObject* cheat = nullptr;        // OLCheatManager
    UObject* worldInfo = nullptr;    // WorldInfo
    UObject* game = nullptr;         // OLGame
    UObject* hud = nullptr;          // OLHUD
    UObject* fx = nullptr;           // OLFXManager
};

const World& W();
void RefreshWorld(UObject* viewportClient);
UObject* FindEngine();

// PlayerController.ConsoleCommand - runs any console command (the same as
// typing it in the UE3 console). Returns the command's text output.
std::string Console(const std::string& command);

// Makes sure OLCheatManager exists and has bCheatsEnabled set (the retail
// build ships with it disabled in OLGame.ini).
bool EnsureCheats();
// Runs a parameterless exec function on the cheat manager (or the player
// controller if the cheat manager does not have it).
bool CheatExec(const char* function);

bool TeleportPlayer(const FVector& location, const FRotator* rotation);
bool LoadCheckpoint(const std::string& checkpointName);
bool GetViewPoint(FVector& location, FRotator& rotation, float& fov);
// Line trace from the camera; returns false when nothing was hit.
bool TraceFromCamera(float distance, FVector& hit);

UObject* LoadObjectByPath(const std::string& path, const char* className);
bool SetSkeletalMesh(UObject* meshComponent, UObject* mesh);
std::string MapName();
bool PlayingDLC();
std::string CurrentCheckpoint();

// Restores original values when a feature is switched off.
class OverrideStore {
public:
    void Float(const std::string& key, UObject* obj, const char* path, bool enable, float value);
    void Int(const std::string& key, UObject* obj, const char* path, bool enable, int32_t value);
    void Bool(const std::string& key, UObject* obj, const char* path, bool enable, bool value);
    void Byte(const std::string& key, UObject* obj, const char* path, bool enable, uint8_t value);
    // Multiplies the value captured when the override started.
    void Scale(const std::string& key, UObject* obj, const char* path, bool enable, float factor);
    // Like Scale/Float but works for float, int and byte properties alike.
    void ScaleNumber(const std::string& key, UObject* obj, const char* path, bool enable, float factor);
    void SetNumber(const std::string& key, UObject* obj, const char* path, bool enable, float value);
    // Raw bytes, for struct properties such as colours.
    void Raw(const std::string& key, UObject* obj, const char* path, bool enable, const void* data, size_t size);
    void RestoreAll();
    void Purge();  // forget entries whose object no longer exists

private:
    enum class Type { Float, Int, Bool, Byte, Raw };
    struct Entry {
        UObject* obj = nullptr;
        int32_t index = -1;
        std::string path;
        Type type = Type::Float;
        float f = 0;
        int32_t i = 0;
        bool b = false;
        uint8_t by = 0;
        std::vector<uint8_t> raw;
    };
    Entry* Begin(const std::string& key, UObject* obj, const char* path, bool enable, Type type);
    void Restore(Entry& e);

    std::unordered_map<std::string, Entry> entries_;
};

OverrideStore& Overrides();

}  // namespace omm::game
