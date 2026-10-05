// Player, movement, camera, battery and character features.
#include "features.h"
#include "outlast.h"

#include "../ue3/call.h"

namespace omm::game {

using namespace ue3;

namespace {
// Every noise source of the hero (OLGame.ini [OLGame.OLHero]).
const char* const kLoudness[] = {
    "WalkingLoudness",        "CrouchLoudness",         "RunningLoudness",         "WalkingWaterLoudness",
    "CrouchWaterLoudness",    "FallingHighLoudness",    "FallingMedLoudness",      "FallingLowLoudness",
    "HobblingWalkLoudness",   "HobblingRunLoudness",    "LandingBigLoudness",      "LandingSmallLoudness",
    "LandingBigWaterLoudness", "LandingSmallWaterLoudness", "DoorOpenInstantLoudness", "DoorOpenPartialLoudness",
    "DoorCloseFastLoudness",  "DoorEnterLockerLoudness", "DoorExitLockerLoudness",  "DoorRunThroughLoudness",
};
}  // namespace

void ApplyPlayerFeatures(const ModState& s) {
    const World& w = W();
    OverrideStore& o = Overrides();
    UObject* pc = w.pc;
    UObject* hero = w.hero;

    // --- Health -------------------------------------------------------------
    o.Bool("god", pc, "bGodMode", s.godMode, true);
    o.Int("maxHealth", hero, "HealthMax", s.overrideMaxHealth, s.maxHealth);
    if (hero && (s.infiniteHealth || s.godMode)) {
        int32_t maxHealth = Int(hero, "HealthMax", 100);
        if (Int(hero, "Health", maxHealth) < maxHealth && Int(hero, "Health", 1) > 0) {
            SetInt(hero, "Health", maxHealth);
            SetFloat(hero, "PreciseHealth", static_cast<float>(maxHealth));
        }
    }
    o.Bool("invisible", hero, "bIsGhost", s.invisible, true);
    for (const char* k : kLoudness) o.Float(std::string("silent:") + k, hero, k, s.silentFootsteps, 0.f);
    o.Float("fallDamage", hero, "FallSpeedForDamage", s.noFallDamage, 1.0e6f);
    o.Float("fallDeath", hero, "FallSpeedForDeath", s.noFallDamage, 1.0e6f);
    o.Float("regenDelay", hero, "HealthRegenDelay", s.fastHealthRegen, s.regenDelay);
    o.Float("regenRate", hero, "HealthRegenRate", s.fastHealthRegen, s.regenRate);

    // --- Movement -------------------------------------------------------------
    const float m = s.speedMultiplier;
    const bool sp = s.speedOverride;
    o.Float("spd:walk", hero, "NormalWalkSpeed", sp, s.walkSpeed * m);
    o.Float("spd:run", hero, "NormalRunSpeed", sp, s.runSpeed * m);
    o.Float("spd:crouch", hero, "CrouchedSpeed", sp, s.crouchSpeed * m);
    o.Float("spd:waterWalk", hero, "WaterWalkSpeed", sp, s.waterWalkSpeed * m);
    o.Float("spd:waterRun", hero, "WaterRunSpeed", sp, s.waterRunSpeed * m);
    o.Float("spd:limp", hero, "LimpingWalkSpeed", sp, s.limpWalkSpeed * m);
    o.Float("spd:hobbleWalk", hero, "HobblingWalkSpeed", sp, s.hobbleWalkSpeed * m);
    o.Float("spd:hobbleRun", hero, "HobblingRunSpeed", sp, s.hobbleRunSpeed * m);
    o.Scale("spd:electrified", hero, "ElectrifiedSpeed", sp, m);
    o.Scale("spd:jumpWalk", hero, "ForwardSpeedForJumpWalking", sp, m);
    o.Scale("spd:jumpRun", hero, "ForwardSpeedForJumpRunning", sp, m);
    o.Float("spd:back", hero, "SpeedPenaltyBackwards", s.noDirectionPenalty, 0.f);
    o.Float("spd:strafe", hero, "SpeedPenaltyStrafe", s.noDirectionPenalty, 0.f);
    o.Scale("jump:walk", hero, "JumpClearanceWalking", s.jumpOverride, s.jumpMultiplier);
    o.Scale("jump:run", hero, "JumpClearanceRunning", s.jumpOverride, s.jumpMultiplier);
    o.Scale("jump:z", hero, "JumpZ", s.jumpOverride, s.jumpMultiplier);
    o.Scale("gravity", w.worldInfo, "WorldGravityZ", s.gravityOverride, s.gravityMultiplier);
    o.Float("noclipSpeed", pc, "DebugFreeCamSpeed", s.noclipSpeed > 0.f, s.noclipSpeed);

    // --- Camera -------------------------------------------------------------
    o.Float("fov", hero, "DefaultFOV", s.fovOverride, s.fov);
    o.Float("runFov", hero, "RunningFOV", s.fovOverride, s.runFov);
    o.Float("camMaxFov", hero, "CamcorderMaxFOV", s.camcorderZoomOverride, s.camcorderMaxFov);
    o.Float("camNvMaxFov", hero, "CamcorderNVMaxFOV", s.camcorderZoomOverride, s.camcorderMaxFov);

    // --- Batteries ----------------------------------------------------------
    if (s.unlimitedBatteries && !w.cheat) EnsureCheats();
    o.Bool("unlimitedBatteries", w.cheat, "bUnlimitedBatteries", s.unlimitedBatteries, true);
    if (s.unlimitedBatteries && hero) {
        // Belt and braces: keep the current battery topped up as well.
        float e = Float(hero, "CurrentBatterySetEnergy", 1.f);
        if (e < 0.98f) SetFloat(hero, "CurrentBatterySetEnergy", 1.f);
    }
    o.Int("maxBatteries", pc, "MaxNumBatteries", s.overrideMaxBatteries, s.maxBatteries);
    if (s.lockBatteryCount && pc) {
        int32_t cap = Int(pc, "MaxNumBatteries", s.batteryCount);
        if (s.batteryCount > cap) SetInt(pc, "MaxNumBatteries", s.batteryCount);
        if (Int(pc, "NumBatteries", -1) != s.batteryCount) SetInt(pc, "NumBatteries", s.batteryCount);
    }
    o.Float("batteryDuration", hero, "BatteryDuration", s.overrideBatteryDuration, s.batteryDuration);
    o.Float("nvRadius", hero, "NVLightZoomedInRadius", s.nightVisionBoost, s.nightVisionRange);
    o.Float("nvBrightness", hero, "NVLightZoomedInBrightness", s.nightVisionBoost, s.nightVisionBrightness);
    o.Float("nvOuter", hero, "NVLightZoomedInOuterAngle", s.nightVisionBoost, 30.f);

    // --- Character ------------------------------------------------------------
    o.Byte("walkingStyle", hero, "ForcedWalkingStyle", s.walkingStyleOverride, static_cast<uint8_t>(s.walkingStyle));
    o.Bool("limp", hero, "bLimping", s.limp, true);
    o.Bool("hobble", hero, "bHobbling", s.hobble, true);
    o.Float("hobbleI", hero, "HobblingIntensity", s.hobble, s.hobbleIntensity);
    o.Float("hobbleT", hero, "TargetHobblingIntensity", s.hobble, s.hobbleIntensity);
    o.Bool("cracked", hero, "bCameraCracked", s.cameraCracked, true);
    UObject* uber = Obj(w.fx, "CurrentUberPostEffect");
    o.Bool("crackedFx", uber, "bCameraGlassShattered", s.cameraCracked, true);
}

void FillPlayerSnapshot(Snapshot& snap) {
    const World& w = W();
    snap.inGame = w.hero != nullptr;
    if (w.worldInfo) {
        snap.timeDilation = Float(w.worldInfo, "TimeDilation", 1.f);
        snap.paused = Obj(w.worldInfo, "Pauser") != nullptr;
    }
    if (w.game) {
        uint8_t diff = 0;
        GetByte(w.game, "DifficultyMode", diff);
        snap.difficulty = diff;
        snap.playingDLC = PlayingDLC();
        snap.checkpoint = CurrentCheckpoint();
    }
    if (w.pc) {
        snap.batteries = Int(w.pc, "NumBatteries");
        snap.maxBatteries = Int(w.pc, "MaxNumBatteries");
        snap.ghost = Bool(w.pc, "bDebugGhost");
        snap.freeCam = Bool(w.pc, "bDebugFreeCam");
        snap.fixedCam = Bool(w.pc, "bDebugFixedCam");
        snap.godMode = Bool(w.pc, "bGodMode");
        snap.hasCamcorder = Bool(w.pc, "bHasCamcorder", true);
        std::vector<FName> names;
        if (GetNameArray(w.pc, "CompletedRecordingMoments", names)) snap.recordings = static_cast<int>(names.size());
        UObject* inv = Obj(w.pc, "InventoryManager");
        if (inv && GetNameArray(inv, "CollectedDocuments", names)) snap.documents = static_cast<int>(names.size());
    }
    if (w.cheat) snap.cheatsEnabled = Bool(w.cheat, "bCheatsEnabled");
    if (w.hero) {
        snap.health = Int(w.hero, "Health");
        snap.healthMax = Int(w.hero, "HealthMax", 100);
        GetVector(w.hero, "Location", snap.location);
        GetRotator(w.hero, "Rotation", snap.rotation);
        FVector v;
        if (GetVector(w.hero, "Velocity", v)) snap.speed = v.Size2D();
        snap.batteryEnergy = Float(w.hero, "CurrentBatterySetEnergy");
        UObject* mesh = Obj(Obj(w.hero, "Mesh"), "SkeletalMesh");
        snap.heroMesh = mesh ? Name(mesh) : std::string();
    }
    float fov = 90.f;
    if (GetViewPoint(snap.camLocation, snap.camRotation, fov)) snap.camFov = fov;
}

}  // namespace omm::game
