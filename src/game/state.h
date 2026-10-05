// Shared state between the menu (render thread) and the game thread.
//
//   ModState  - everything the user can change. The UI edits its own copy
//               and publishes it; the game thread applies the latest copy
//               every frame.
//   Snapshot  - what the game thread observed this frame (player stats,
//               camera, ESP targets...). The UI only ever reads copies.
//   Commands  - one-shot actions queued by the UI and run on the game thread.
#pragma once

#include "../ue3/types.h"

#include <functional>
#include <string>
#include <vector>

namespace omm::game {

enum class EspCategory : uint8_t {
    Enemy = 0,
    Battery,
    Document,
    Recording,
    KeyItem,
    HidingSpot,
    Checkpoint,
    Door,
    Count
};
const char* EspCategoryName(EspCategory c);

struct EspStyle {
    bool enabled = false;
    float color[4] = {1, 1, 1, 1};
};

struct ModState {
    // --- Player ------------------------------------------------------------
    bool godMode = false;
    bool infiniteHealth = false;
    bool invisible = false;          // OLHero.bIsGhost: AI ignores the player
    bool silentFootsteps = false;    // all hero loudness values -> 0
    bool noFallDamage = false;
    bool fastHealthRegen = false;
    float regenDelay = 10.f;         // game default 10
    float regenRate = 10.f;          // game default 10 HP/s
    bool overrideMaxHealth = false;
    int maxHealth = 100;

    // --- Movement ------------------------------------------------------------
    bool speedOverride = false;
    float speedMultiplier = 1.0f;
    float walkSpeed = 200.f;         // OLHero defaults from OLGame.ini
    float runSpeed = 450.f;
    float crouchSpeed = 75.f;
    float waterWalkSpeed = 100.f;
    float waterRunSpeed = 200.f;
    float limpWalkSpeed = 87.243f;
    float hobbleWalkSpeed = 140.f;
    float hobbleRunSpeed = 250.f;
    bool noDirectionPenalty = false;  // full speed backwards/sideways
    bool jumpOverride = false;
    float jumpMultiplier = 1.0f;
    bool gravityOverride = false;
    float gravityMultiplier = 1.0f;
    float noclipSpeed = 0.f;          // 0 = leave the game's free-cam speed alone

    // --- Camera --------------------------------------------------------------
    bool fovOverride = false;
    float fov = 90.f;
    float runFov = 100.f;
    bool camcorderZoomOverride = false;
    float camcorderMaxFov = 83.f;

    // --- Batteries / camcorder -----------------------------------------------
    bool unlimitedBatteries = false;
    bool lockBatteryCount = false;
    int batteryCount = 10;
    bool overrideMaxBatteries = false;
    int maxBatteries = 10;
    bool overrideBatteryDuration = false;
    float batteryDuration = 150.f;   // seconds per battery (game default 150)
    bool nightVisionBoost = false;
    float nightVisionRange = 1500.f;
    float nightVisionBrightness = 0.025f;

    // --- World ---------------------------------------------------------------
    bool gameSpeedOverride = false;
    float gameSpeed = 1.0f;
    bool freezeEnemies = false;
    bool enemyTimeScaleOverride = false;
    float enemyTimeScale = 1.0f;
    bool passiveEnemies = false;
    bool blindEnemies = false;
    bool deafEnemies = false;
    bool enemySpeedOverride = false;
    float enemySpeedMultiplier = 1.0f;
    bool enemyDamageOverride = false;
    float enemyDamageMultiplier = 1.0f;
    bool invisibleEnemies = false;

    // --- Visuals -------------------------------------------------------------
    bool gammaOverride = false;
    float gamma = 2.2f;
    bool brightnessOverride = false;
    float shadowsMul = 1.0f;         // LocalPlayer post-process multipliers
    float midtonesMul = 1.0f;
    float highlightsMul = 1.0f;
    float desaturationMul = 1.0f;
    bool darkLightOverride = false;  // light that follows the player
    float darkLightRadius = 600.f;
    float darkLightBrightness = 0.15f;
    bool noFilmGrain = false;
    bool noVignette = false;
    bool noHurtEffect = false;
    bool tintOverride = false;
    float tint[4] = {1, 1, 1, 1};
    bool hideHud = false;
    bool hideCrosshair = false;
    bool fpsLimitOverride = false;
    float fpsLimit = 62.f;

    // --- ESP -------------------------------------------------------------------
    bool espEnabled = false;
    EspStyle esp[static_cast<int>(EspCategory::Count)];
    float espMaxDistance = 6000.f;
    bool espShowDistance = true;
    bool espShowLabels = true;
    bool espTracers = false;
    bool espHideCollected = true;
    bool espOffscreenArrows = true;

    // --- Character -------------------------------------------------------------
    bool walkingStyleOverride = false;
    int walkingStyle = 0;            // EHeroWalkingStyle
    bool limp = false;
    bool hobble = false;
    float hobbleIntensity = 0.5f;
    bool cameraCracked = false;

    // --- Enemies / fun -----------------------------------------------------------
    bool hordeMode = false;
    float hordeInterval = 45.f;
    int hordeMaxEnemies = 6;
    int hordeEnemyType = 0;          // index into SpawnableEnemies(); -1 = random
    bool autoApplyModelSwaps = true;
    bool enemyScaleOverride = false;
    float enemyScale[3] = {1, 1, 1};

    // --- Menu ----------------------------------------------------------------
    bool pauseWhileMenuOpen = false;
    bool showModMenuButton = true;   // button drawn on the game's own menus
    int menuButtonCorner = 1;        // 0 TL, 1 TR, 2 BL, 3 BR
    bool notifications = true;
    float uiScale = 1.0f;
};

void InitEspDefaults(ModState& s);

struct EspItem {
    EspCategory category = EspCategory::Enemy;
    ue3::FVector location;
    float height = 0.f;              // for boxes (0 = point marker)
    float distance = 0.f;
    std::string label;
    std::string detail;
    bool collected = false;
};

struct EnemyInfo {
    int32_t index = -1;              // object index (stable id for commands)
    std::string className;
    std::string displayName;
    std::string state;
    std::string mesh;
    ue3::FVector location;
    float distance = 0.f;
    int health = 0;
    bool frozen = false;
    bool passive = false;
    bool spawnedByMod = false;
};

enum class GameMenu { None, MainMenu, Pause, Options, Other };

struct Snapshot {
    bool engineReady = false;
    bool inGame = false;             // a hero pawn exists
    GameMenu menu = GameMenu::None;
    std::string menuView;
    bool paused = false;
    std::string mapName;
    std::string checkpoint;
    bool playingDLC = false;
    int difficulty = 0;

    // Player
    int health = 0;
    int healthMax = 100;
    ue3::FVector location;
    ue3::FRotator rotation;
    float speed = 0.f;
    int batteries = 0;
    int maxBatteries = 0;
    float batteryEnergy = 0.f;
    bool ghost = false;
    bool freeCam = false;
    bool fixedCam = false;
    bool godMode = false;
    bool cheatsEnabled = false;
    bool hasCamcorder = false;
    std::string heroMesh;
    float timeDilation = 1.f;
    int documents = 0;
    int recordings = 0;

    // Camera (for ESP projection on the render thread)
    ue3::FVector camLocation;
    ue3::FRotator camRotation;
    float camFov = 90.f;

    std::vector<EspItem> esp;
    std::vector<EnemyInfo> enemies;
    int modSpawnedEnemies = 0;

    // Diagnostics
    std::string engineStatus;
    uint64_t frame = 0;
};

namespace state {
ModState& Ui();               // render-thread copy (bound to widgets)
void Publish();               // Ui() -> shared
ModState Shared();            // game thread: latest published copy
void Load();                  // from config.ini
void Save();                  // to config.ini

void PublishSnapshot(Snapshot&& s);
Snapshot GetSnapshot();
}  // namespace state

// One-shot actions, run on the game thread at the start of the next frame.
void Enqueue(std::function<void()> fn);
void RunQueued();

// On-screen notifications (thread-safe).
void Notify(const std::string& text, float seconds = 2.5f);
struct Toast {
    std::string text;
    uint64_t expiresMs;
};
std::vector<Toast> ActiveToasts();

}  // namespace omm::game
