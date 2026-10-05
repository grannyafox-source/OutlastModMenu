// End-to-end test of the mod inside a Windows process (native Windows or
// Wine), without the game.
//
// The executable contains a simulated Unreal Engine 3 world (tests/fake_ue3.h)
// published in its own data section exactly where the mod looks for the real
// engine's tables, a working UObject::ProcessEvent stand-in reached through
// every object's vtable, and native functions that behave like Outlast's
// (GetPlayerViewPoint, ConsoleCommand, Ghost...). The mod's real start-up
// thread runs, finds the engine, hooks GameViewportClient.PostRender, and the
// test then drives options and actions the way the menu does and checks their
// effect on the simulated objects.
//
// Build and run: tests/win/run.sh
#include "../fake_ue3.h"

#include "../../src/app/app.h"
#include "../../src/core/guard.h"
#include "../../src/core/log.h"
#include "../../src/core/strutil.h"
#include "../../src/game/actions.h"
#include "../../src/game/enemies.h"
#include "../../src/game/outlast.h"
#include "../../src/game/inspector.h"
#include "../../src/game/state.h"
#include "../../src/ui/ui.h"
#include "../../src/ue3/call.h"
#include "../../src/ue3/engine.h"
#include "../../src/ue3/hooks.h"
#include "../../sdk/omm_plugin_api.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

extern "C" const OMM_Api* OMM_GetApi(void);

namespace {

constexpr size_t P = sizeof(void*);
#if defined(__x86_64__)
#define OMM_TEST_THISCALL
#else
#define OMM_TEST_THISCALL __attribute__((thiscall))
#endif
using NativeFn = void(OMM_TEST_THISCALL*)(void* self, void* stack, void* result);

// The engine's global tables must be in the executable's writable data, like
// GNames/GObjects in OLGame.exe.
alignas(16) uint8_t g_engineData[0x10000];

fake::Config g_cfg;
fake::World* g_w = nullptr;
fake::Graph g_g;

int g_checks = 0, g_failures = 0;
#define EXPECT(cond)                                                     \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            ++g_failures;                                                \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);        \
        }                                                                \
    } while (0)

template <typename T>
T Get(uintptr_t a) {
    T v;
    std::memcpy(&v, reinterpret_cast<void*>(a), sizeof(T));
    return v;
}
template <typename T>
void Put(uintptr_t a, const T& v) {
    std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(T));
}

// --- ProcessEvent stand-in ------------------------------------------------------------
struct CallCtx {
    uintptr_t fn;
    uint8_t* parms;
};
std::vector<CallCtx> g_calls;
std::map<std::string, int32_t> g_paramOff;  // "Function.Param" -> offset in the parameter block
uintptr_t g_vtable[96];

void OMM_TEST_THISCALL DummyVirtual(void*) {}

// UObject::ProcessEvent: builds a frame and calls the function's code pointer.
__attribute__((noinline)) void OMM_TEST_THISCALL FakeProcessEvent(void* self, void* fn, void* parms, void* result) {
    g_calls.push_back({reinterpret_cast<uintptr_t>(fn), static_cast<uint8_t*>(parms)});
    uintptr_t func = Get<uintptr_t>(reinterpret_cast<uintptr_t>(fn) + g_cfg.funcFunc());
    alignas(16) uint8_t frame[128] = {};
    std::memcpy(frame + P, &fn, P);        // FFrame::Node
    std::memcpy(frame + 2 * P, &self, P);  // FFrame::Object
    reinterpret_cast<NativeFn>(func)(self, frame, result);
    g_calls.pop_back();
}

uint8_t* Parms() { return g_calls.back().parms; }
int32_t Off(const char* fnParam) {
    auto it = g_paramOff.find(fnParam);
    if (it == g_paramOff.end()) {
        std::printf("  harness: unknown parameter %s\n", fnParam);
        std::abort();
    }
    return it->second;
}

// --- The simulated world ----------------------------------------------------------------
struct Objs {
    uintptr_t engine, viewport, localPlayer, pc, hero, heroMesh, worldInfo, game, hud, menuManager, optionsView,
        cheat, enemy, enemyArchetype, bot, sight, cylinder, enemyMesh, battery, soldierMesh, priestMesh,
        fingerlessMesh, normalMesh, postRender, cheatClass, level, soldierClass, botClass, sightClass, cylClass,
        skelCompClass, soldierBT;
} o;

struct Offsets {
    int32_t location, rotation, bHidden, worldInfo, bDeleteMe, velocity, customTD, drawScale3D;
    int32_t pawnController, health, healthMax, mesh, cylinder;
    int32_t pcHero, pcCheat, pcHud, pcFx, pcInv, numBatt, maxBatt, debugFlags, godFlags, camPos, camRot,
        freeCamSpeed, pri;
    int32_t walk, run, crouch, bIsGhost, battEnergy, battDuration, defFov, fingerless, preciseHealth;
    int32_t timeDilation, pauser, wiGame, gravity;
    int32_t gameDlc, checkpoint;
    int32_t hudMenu, menuOpen, menuType, viewStack;
    int32_t cheatFlags;
    int32_t skelMesh;
    int32_t botState, botSight, sightIgnore, collisionHeight, hearing, enemyDamage;
    int32_t battCount, battUsed;
    int32_t behaviorTree, modifiers, limping, walkStyle;
} off;

std::string g_lastConsole, g_lastCheckpoint;
int g_crashCalls = 0;
int g_spawned = 0;

uintptr_t Class(const char* name, uintptr_t super) {
    uintptr_t c = g_w->Object(name, g_g.classClass, g_g.engine, g_cfg.structSize() + 0x40);
    Put<uintptr_t>(c + g_cfg.structSuper(), super);
    return c;
}

uintptr_t Prop(uintptr_t owner, const char* name, const char* type, int32_t offset, int32_t size,
               uintptr_t extra = 0, uint64_t flags = 0) {
    uintptr_t p = g_w->Object(name, g_g.propClasses[type], owner, g_cfg.propSize() + 2 * P);
    Put<int32_t>(p + g_cfg.propArrayDim(), 1);
    Put<int32_t>(p + g_cfg.propElementSize(), size);
    Put<uint64_t>(p + g_cfg.propFlags(), flags);
    Put<int32_t>(p + g_cfg.propOffset(), offset);
    Put<uintptr_t>(p + g_cfg.propSize(), extra);
    g_w->AddChild(owner, p);
    return p;
}

uintptr_t ArrayProp(uintptr_t owner, const char* name, const char* innerType, int32_t offset) {
    uintptr_t inner = g_w->Object(name, g_g.propClasses[innerType], owner, g_cfg.propSize() + 2 * P);
    Put<int32_t>(inner + g_cfg.propArrayDim(), 1);
    Put<int32_t>(inner + g_cfg.propElementSize(), std::strcmp(innerType, "NameProperty") == 0 ? 8 : static_cast<int32_t>(P));
    return Prop(owner, name, "ArrayProperty", offset, static_cast<int32_t>(P + 8), inner);
}

struct Param {
    const char* name;
    const char* type;
    int32_t size;
    uintptr_t extra;
};

// Defines a function with its parameter list (laid out like the compiler
// does) and points its code pointer at `code`.
uintptr_t Function(uintptr_t owner, const char* name, uint32_t flags, NativeFn code, std::vector<Param> params) {
    uintptr_t f = g_w->Object(name, g_g.functionClass, owner, g_cfg.funcSize() + 0x10);
    Put<uint32_t>(f + g_cfg.funcFlags(), flags);
    int32_t at = 0, ret = 0xFFFF;
    for (const Param& p : params) {
        int32_t align = p.size >= static_cast<int32_t>(P) ? static_cast<int32_t>(P) : 4;
        at = (at + align - 1) & ~(align - 1);
        uint64_t pf = 0x80 | (std::strcmp(p.name, "ReturnValue") == 0 ? 0x400 : 0);
        Prop(f, p.name, p.type, at, p.size, p.extra, pf);
        g_paramOff[std::string(name) + "." + p.name] = at;
        if (std::strcmp(p.name, "ReturnValue") == 0) ret = at;
        at += p.size;
    }
    Put<uint16_t>(f + g_cfg.funcParms(), static_cast<uint16_t>(at));
    Put<uint16_t>(f + g_cfg.funcRet(), static_cast<uint16_t>(ret));
    Put<uintptr_t>(f + g_cfg.funcFunc(), reinterpret_cast<uintptr_t>(code));
    g_w->AddChild(owner, f);
    return f;
}

void OMM_TEST_THISCALL ScriptBody(void*, void*, void*) {}  // UObject::ProcessInternal
void OMM_TEST_THISCALL NativeNop(void*, void*, void*) {}

void PutStrReturn(int32_t at, const char* text) {
    std::u16string s = omm::str::Utf8ToUtf16(text);
    auto* data = new char16_t[s.size() + 1];  // leaked, like the engine's allocations
    std::memcpy(data, s.c_str(), (s.size() + 1) * sizeof(char16_t));
    omm::ue3::FString f;
    f.Data = data;
    f.Num = f.Max = static_cast<int32_t>(s.size() + 1);
    std::memcpy(Parms() + at, &f, sizeof(f));
}

std::string GetStrParm(int32_t at) {
    omm::ue3::FString f;
    std::memcpy(&f, Parms() + at, sizeof(f));
    if (!f.Data || f.Num <= 0) return std::string();
    return omm::str::Utf16ToUtf8(f.Data, static_cast<size_t>(f.Num - 1));
}

// --- Natives ------------------------------------------------------------------------------
void OMM_TEST_THISCALL N_SetLocation(void* self, void*, void*) {
    uintptr_t a = reinterpret_cast<uintptr_t>(self);
    std::memcpy(reinterpret_cast<void*>(a + off.location), Parms() + Off("SetLocation.NewLocation"), 12);
    Put<uint32_t>(reinterpret_cast<uintptr_t>(Parms()) + Off("SetLocation.ReturnValue"), 1);
}
void OMM_TEST_THISCALL N_GetPlayerViewPoint(void*, void*, void*) {
    omm::ue3::FVector loc = Get<omm::ue3::FVector>(o.hero + off.location);
    loc.Z += 64.f;
    omm::ue3::FRotator rot{0, 16384, 0};
    std::memcpy(Parms() + Off("GetPlayerViewPoint.POVLocation"), &loc, 12);
    std::memcpy(Parms() + Off("GetPlayerViewPoint.POVRotation"), &rot, 12);
}
void OMM_TEST_THISCALL N_GetFOVAngle(void*, void*, void*) {
    float fov = 90.f;
    std::memcpy(Parms() + Off("GetFOVAngle.ReturnValue"), &fov, 4);
}
void OMM_TEST_THISCALL N_ConsoleCommand(void*, void*, void*) {
    g_lastConsole = GetStrParm(Off("ConsoleCommand.Command"));
    PutStrReturn(Off("ConsoleCommand.ReturnValue"), ("ok: " + g_lastConsole).c_str());
}
void OMM_TEST_THISCALL N_ClientSetLocation(void*, void*, void*) {
    std::memcpy(reinterpret_cast<void*>(o.hero + off.location), Parms() + Off("ClientSetLocation.NewLocation"), 12);
}
void OMM_TEST_THISCALL N_AddCheats(void*, void*, void*) { Put<uintptr_t>(o.pc + off.pcCheat, o.cheat); }
void OMM_TEST_THISCALL N_Ghost(void*, void*, void*) {
    Put<uint32_t>(o.pc + off.debugFlags, Get<uint32_t>(o.pc + off.debugFlags) ^ 1u);
}
void OMM_TEST_THISCALL N_ToggleFreeCamNoPause(void*, void*, void*) {
    Put<uint32_t>(o.pc + off.debugFlags, Get<uint32_t>(o.pc + off.debugFlags) ^ 2u);
}
void OMM_TEST_THISCALL N_StartNewGameAtCheckpoint(void*, void*, void*) {
    g_lastCheckpoint = GetStrParm(Off("StartNewGameAtCheckpoint.CheckpointStr"));
}
void OMM_TEST_THISCALL N_GetMapName(void*, void*, void*) { PutStrReturn(Off("GetMapName.ReturnValue"), "FakeMap_P"); }
void OMM_TEST_THISCALL N_SetSkeletalMesh(void* self, void*, void*) {
    uintptr_t mesh = Get<uintptr_t>(reinterpret_cast<uintptr_t>(Parms()) + Off("SetSkeletalMesh.NewMesh"));
    Put<uintptr_t>(reinterpret_cast<uintptr_t>(self) + off.skelMesh, mesh);
}
void OMM_TEST_THISCALL N_SetHidden(void* self, void*, void*) {
    uint32_t v = Get<uint32_t>(reinterpret_cast<uintptr_t>(Parms()) + Off("SetHidden.bNewHidden")) & 1u;
    uintptr_t a = reinterpret_cast<uintptr_t>(self) + off.bHidden;
    Put<uint32_t>(a, (Get<uint32_t>(a) & ~1u) | v);
}
uintptr_t NewActor(uintptr_t cls, const char* base, size_t size = 0x1000) {
    uintptr_t a = g_w->SpawnObject(base, ++g_spawned, cls, o.level, size);
    Put<uintptr_t>(a, reinterpret_cast<uintptr_t>(g_vtable));
    Put<uintptr_t>(a + off.worldInfo, o.worldInfo);
    Put<float>(a + off.customTD, 1.f);
    return a;
}
void OMM_TEST_THISCALL N_Spawn(void*, void*, void*) {
    uintptr_t p = reinterpret_cast<uintptr_t>(Parms());
    uintptr_t cls = Get<uintptr_t>(p + Off("Spawn.SpawnClass"));
    uintptr_t a = NewActor(cls, cls == o.botClass ? "OLBot" : "OLEnemySoldier");
    std::memcpy(reinterpret_cast<void*>(a + off.location), Parms() + Off("Spawn.SpawnLocation"), 12);
    if (cls == o.soldierClass) {
        uintptr_t cyl = g_w->SpawnObject("CylinderComponent", ++g_spawned, o.cylClass, a, 0x600);
        uintptr_t mesh = g_w->SpawnObject("SkeletalMeshComponent", ++g_spawned, o.skelCompClass, a, 0x600);
        Put<uintptr_t>(cyl, reinterpret_cast<uintptr_t>(g_vtable));
        Put<uintptr_t>(mesh, reinterpret_cast<uintptr_t>(g_vtable));
        Put<float>(cyl + off.collisionHeight, 88.f);
        Put<uintptr_t>(a + off.cylinder, cyl);
        Put<uintptr_t>(a + off.mesh, mesh);
        Put<int32_t>(a + off.health, 100);
        Put<float>(a + off.hearing, 2000.f);
    } else if (cls == o.botClass) {
        uintptr_t sight = g_w->SpawnObject("OLAISightComponent", ++g_spawned, o.sightClass, a, 0x600);
        Put<uintptr_t>(sight, reinterpret_cast<uintptr_t>(g_vtable));
        Put<uintptr_t>(a + off.botSight, sight);
    }
    Put<uintptr_t>(p + Off("Spawn.ReturnValue"), a);
}
void OMM_TEST_THISCALL N_Possess(void* self, void*, void*) {
    uintptr_t pawn = Get<uintptr_t>(reinterpret_cast<uintptr_t>(Parms()) + Off("Possess.inPawn"));
    Put<uintptr_t>(reinterpret_cast<uintptr_t>(self) + off.pawnController, pawn);  // Controller.Pawn
    Put<uintptr_t>(pawn + off.pawnController, reinterpret_cast<uintptr_t>(self));  // Pawn.Controller
}
void OMM_TEST_THISCALL N_ApplyModifiers(void* self, void*, void*) {
    std::memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(self) + off.modifiers),
                Parms() + Off("ApplyModifiers.NewModifiers"), 8);
}
void OMM_TEST_THISCALL N_Destroy(void* self, void*, void*) {
    uintptr_t a = reinterpret_cast<uintptr_t>(self) + off.bDeleteMe;
    Put<uint32_t>(a, Get<uint32_t>(a) | 1u);
}
void OMM_TEST_THISCALL N_Trace(void*, void*, void*) {
    omm::ue3::FVector hit(1800, 2400, 300), normal(0, 0, 1);
    std::memcpy(Parms() + Off("Trace.HitLocation"), &hit, 12);
    std::memcpy(Parms() + Off("Trace.HitNormal"), &normal, 12);
}

void OMM_TEST_THISCALL N_CrashTest(void*, void*, void*) {
    ++g_crashCalls;
    *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x20)) = 1;  // access violation on purpose
}

void Build() {
    g_w = new fake::World(g_cfg, g_engineData, sizeof(g_engineData));
    g_g = fake::Build(*g_w);
    fake::World& w = *g_w;
    const uintptr_t vecS = g_g.vectorStruct, rotS = g_g.rotatorStruct;
    const int32_t ip = static_cast<int32_t>(P);

    // Every function built by fake::Build gets real code: script functions
    // share ProcessInternal, natives a do-nothing thunk.
    for (uintptr_t obj : w.Objects())
        if (obj && Get<uintptr_t>(obj + g_cfg.objClass) == g_g.functionClass) {
            uint32_t flags = Get<uint32_t>(obj + g_cfg.funcFlags());
            Put<uintptr_t>(obj + g_cfg.funcFunc(), reinterpret_cast<uintptr_t>(flags & 0x400 ? &NativeNop : &ScriptBody));
        }
    // Actor.SetLocation (already defined by fake::Build) does real work.
    Put<uintptr_t>(g_g.setLocation + g_cfg.funcFunc(), reinterpret_cast<uintptr_t>(&N_SetLocation));
    g_paramOff["SetLocation.NewLocation"] = 0;
    g_paramOff["SetLocation.ReturnValue"] = 12;

    off.location = Get<int32_t>(g_g.location + g_cfg.propOffset());
    off.rotation = off.location + 12;
    off.drawScale3D = off.location + 28;
    off.bHidden = off.location + 56 + ip;
    off.pawnController = 0x300;
    off.health = 0x330;

    // Actor / Pawn / Controller additions.
    off.worldInfo = 0x500;
    off.bDeleteMe = 0x508;
    off.velocity = 0x50C;
    off.customTD = 0x518;
    uintptr_t worldInfoClass = Class("WorldInfo", g_g.actor);
    Prop(g_g.actor, "WorldInfo", "ObjectProperty", off.worldInfo, ip, worldInfoClass);
    Prop(g_g.actor, "bDeleteMe", "BoolProperty", off.bDeleteMe, 4, 1);
    Prop(g_g.actor, "Velocity", "StructProperty", off.velocity, 12, vecS);
    Prop(g_g.actor, "CustomTimeDilation", "FloatProperty", off.customTD, 4);
    Function(g_g.actor, "SetHidden", 0x401, &N_SetHidden, {{"bNewHidden", "BoolProperty", 4, 1}});
    off.healthMax = 0x338;
    off.mesh = 0x600;
    off.cylinder = 0x608;
    Prop(g_g.pawn, "HealthMax", "IntProperty", off.healthMax, 4);
    uintptr_t skelCompClass = Class("SkeletalMeshComponent", g_g.objectClass);
    uintptr_t skelMeshClass = Class("SkeletalMesh", g_g.objectClass);
    uintptr_t cylClass = Class("CylinderComponent", g_g.objectClass);
    Prop(g_g.pawn, "Mesh", "ObjectProperty", off.mesh, ip, skelCompClass);
    Prop(g_g.pawn, "CylinderComponent", "ObjectProperty", off.cylinder, ip, cylClass);
    off.skelMesh = 0x500;
    Prop(skelCompClass, "SkeletalMesh", "ObjectProperty", off.skelMesh, ip, skelMeshClass);
    Function(skelCompClass, "SetSkeletalMesh", 0x401, &N_SetSkeletalMesh,
             {{"NewMesh", "ObjectProperty", ip, skelMeshClass}, {"bKeepSpaceBases", "BoolProperty", 4, 1}});
    off.collisionHeight = 0x500;
    Prop(cylClass, "CollisionHeight", "FloatProperty", off.collisionHeight, 4);

    // Engine, viewport, local player.
    uintptr_t gameEngine = Class("GameEngine", g_g.objectClass);
    uintptr_t olEngine = Class("OLEngine", gameEngine);
    uintptr_t vpClass = Class("GameViewportClient", g_g.objectClass);
    uintptr_t olVp = Class("OLGameViewportClient", vpClass);
    uintptr_t playerClass = Class("Player", g_g.objectClass);
    uintptr_t lpClass = Class("LocalPlayer", playerClass);
    uintptr_t canvasClass = Class("Canvas", g_g.objectClass);
    Prop(gameEngine, "GameViewport", "ObjectProperty", 0x500, ip, vpClass);
    ArrayProp(gameEngine, "GamePlayers", "ObjectProperty", 0x508);
    o.postRender = Function(vpClass, "PostRender", 0x802, &ScriptBody, {{"Canvas", "ObjectProperty", ip, canvasClass}});
    Prop(lpClass, "Actor", "ObjectProperty", 0x500, ip, g_g.playerController);

    // Player controller.
    uintptr_t olPc = Class("OLPlayerController", g_g.playerController);
    uintptr_t heroClass = Class("OLHero", g_g.pawn);
    uintptr_t olCheat = Class("OLCheatManager", g_g.cheatManager);
    uintptr_t hudClass = Class("OLHUD", g_g.actor);
    uintptr_t olGameClass = Class("OLGame", g_g.actor);
    uintptr_t priClass = Class("PlayerReplicationInfo", g_g.actor);
    off.pcHero = 0x700;
    off.pcCheat = 0x708;
    off.pcHud = 0x710;
    off.pcFx = 0x718;
    off.pcInv = 0x720;
    off.numBatt = 0x728;
    off.maxBatt = 0x72C;
    off.debugFlags = 0x730;
    off.godFlags = 0x734;
    off.camPos = 0x738;
    off.camRot = 0x744;
    off.freeCamSpeed = 0x750;
    off.pri = 0x768;
    Prop(olPc, "HeroPawn", "ObjectProperty", off.pcHero, ip, heroClass);
    Prop(g_g.controller, "CheatManager", "ObjectProperty", off.pcCheat, ip, g_g.cheatManager);
    Prop(g_g.controller, "myHUD", "ObjectProperty", off.pcHud, ip, hudClass);
    Prop(olPc, "NumBatteries", "IntProperty", off.numBatt, 4);
    Prop(olPc, "MaxNumBatteries", "IntProperty", off.maxBatt, 4);
    Prop(olPc, "bDebugGhost", "BoolProperty", off.debugFlags, 4, 1);
    Prop(olPc, "bDebugFreeCam", "BoolProperty", off.debugFlags, 4, 2);
    Prop(olPc, "bDebugFixedCam", "BoolProperty", off.debugFlags, 4, 4);
    Prop(olPc, "bHasCamcorder", "BoolProperty", off.debugFlags, 4, 8);
    Prop(g_g.controller, "bGodMode", "BoolProperty", off.godFlags, 4, 1);
    Prop(olPc, "DebugCamPos", "StructProperty", off.camPos, 12, vecS);
    Prop(olPc, "DebugCamRot", "StructProperty", off.camRot, 12, rotS);
    Prop(olPc, "DebugFreeCamSpeed", "FloatProperty", off.freeCamSpeed, 4);
    Prop(g_g.controller, "PlayerReplicationInfo", "ObjectProperty", off.pri, ip, priClass);
    ArrayProp(olPc, "CompletedRecordingMoments", "NameProperty", 0x758);
    Function(g_g.playerController, "GetPlayerViewPoint", 0x800, &N_GetPlayerViewPoint,
             {{"POVLocation", "StructProperty", 12, vecS}, {"POVRotation", "StructProperty", 12, rotS}});
    Function(g_g.playerController, "GetFOVAngle", 0x0, &N_GetFOVAngle, {{"ReturnValue", "FloatProperty", 4, 0}});
    Function(g_g.playerController, "ConsoleCommand", 0x400, &N_ConsoleCommand,
             {{"Command", "StrProperty", ip + 8, 0}, {"bWriteToLog", "BoolProperty", 4, 1},
              {"ReturnValue", "StrProperty", ip + 8, 0}});
    Function(g_g.playerController, "ClientSetLocation", 0x0, &N_ClientSetLocation,
             {{"NewLocation", "StructProperty", 12, vecS}, {"NewRotation", "StructProperty", 12, rotS}});
    Function(g_g.playerController, "AddCheats", 0x0, &N_AddCheats, {{"bForce", "BoolProperty", 4, 1}});
    Function(olPc, "StartNewGameAtCheckpoint", 0x400, &N_StartNewGameAtCheckpoint,
             {{"CheckpointStr", "StrProperty", ip + 8, 0}, {"bSaveToDisk", "BoolProperty", 4, 1}});
    Function(olPc, "CrashTest", 0x401, &N_CrashTest, {});

    // Cheat manager.
    off.cheatFlags = 0x500;
    Prop(olCheat, "bCheatsEnabled", "BoolProperty", off.cheatFlags, 4, 1);
    Prop(olCheat, "bUnlimitedBatteries", "BoolProperty", off.cheatFlags, 4, 2);
    Function(olCheat, "Ghost", 0x200, &N_Ghost, {});
    Function(olCheat, "ToggleFreeCamNoPause", 0x200, &N_ToggleFreeCamNoPause, {});
    o.cheatClass = olCheat;

    // Hero.
    off.walk = 0x800;
    off.run = 0x804;
    off.crouch = 0x808;
    off.preciseHealth = 0x80C;
    off.bIsGhost = 0x810;
    off.battEnergy = 0x820;
    off.battDuration = 0x824;
    off.defFov = 0x828;
    off.fingerless = 0x830;
    Prop(heroClass, "NormalWalkSpeed", "FloatProperty", off.walk, 4);
    Prop(heroClass, "NormalRunSpeed", "FloatProperty", off.run, 4);
    Prop(heroClass, "CrouchedSpeed", "FloatProperty", off.crouch, 4);
    Prop(heroClass, "PreciseHealth", "FloatProperty", off.preciseHealth, 4);
    Prop(heroClass, "bIsGhost", "BoolProperty", off.bIsGhost, 4, 1);
    Prop(heroClass, "CurrentBatterySetEnergy", "FloatProperty", off.battEnergy, 4);
    Prop(heroClass, "BatteryDuration", "FloatProperty", off.battDuration, 4);
    Prop(heroClass, "DefaultFOV", "FloatProperty", off.defFov, 4);
    Prop(heroClass, "FingerlessMesh", "ObjectProperty", off.fingerless, ip, skelMeshClass);
    Function(heroClass, "ResetAfterTeleport", 0x400, &NativeNop, {});

    // World.
    off.timeDilation = 0x600;
    off.pauser = 0x608;
    off.wiGame = 0x610;
    off.gravity = 0x618;
    Prop(worldInfoClass, "TimeDilation", "FloatProperty", off.timeDilation, 4);
    Prop(worldInfoClass, "Pauser", "ObjectProperty", off.pauser, ip, priClass);
    Prop(worldInfoClass, "Game", "ObjectProperty", off.wiGame, ip, olGameClass);
    Prop(worldInfoClass, "WorldGravityZ", "FloatProperty", off.gravity, 4);
    Function(worldInfoClass, "GetMapName", 0x400, &N_GetMapName,
             {{"bIncludePrefix", "BoolProperty", 4, 1}, {"ReturnValue", "StrProperty", ip + 8, 0}});
    off.gameDlc = 0x600;
    off.checkpoint = 0x608;
    Prop(olGameClass, "bIsPlayingDLC", "BoolProperty", off.gameDlc, 4, 1);
    Prop(olGameClass, "CurrentCheckpointName", "NameProperty", off.checkpoint, 8);

    // HUD and the Scaleform menu manager.
    uintptr_t frontEnd = Class("OLUIFrontEnd", g_g.objectClass);
    uintptr_t optionsClass = Class("OLUIFrontEnd_Options", g_g.objectClass);
    off.hudMenu = 0x600;
    off.menuOpen = 0x500;
    off.menuType = 0x504;
    off.viewStack = 0x508;
    Prop(hudClass, "MenuManager", "ObjectProperty", off.hudMenu, ip, frontEnd);
    Prop(frontEnd, "bMovieIsOpen", "BoolProperty", off.menuOpen, 4, 1);
    Prop(frontEnd, "MenuType", "ByteProperty", off.menuType, 1);
    ArrayProp(frontEnd, "ViewStack", "ObjectProperty", off.viewStack);

    // Enemies.
    uintptr_t enemyPawn = Class("OLEnemyPawn", g_g.pawn);
    uintptr_t soldier = Class("OLEnemySoldier", enemyPawn);
    uintptr_t botClass = Class("OLBot", g_g.controller);
    uintptr_t sightClass = Class("OLAISightComponent", g_g.objectClass);
    off.botState = 0x700;
    off.botSight = 0x708;
    off.sightIgnore = 0x500;
    off.hearing = 0x900;
    off.enemyDamage = 0x904;
    Prop(botClass, "BehaviorState", "ByteProperty", off.botState, 1);
    Prop(botClass, "SightComponent", "ObjectProperty", off.botSight, ip, sightClass);
    Prop(sightClass, "bIgnoreTarget", "BoolProperty", off.sightIgnore, 4, 1);
    Prop(g_g.pawn, "HearingThreshold", "FloatProperty", off.hearing, 4);
    Prop(enemyPawn, "AttackNormalDamage", "FloatProperty", off.enemyDamage, 4);
    Function(g_g.actor, "Destroy2", 0x401, &NativeNop, {});

    // ESP target.
    uintptr_t batteryClass = Class("OLBatteriesPickupFactory", g_g.actor);
    off.battCount = 0x600;
    off.battUsed = 0x604;
    Prop(batteryClass, "NumBatteries", "IntProperty", off.battCount, 4);
    Prop(batteryClass, "bUsed", "BoolProperty", off.battUsed, 4, 1);

    // Spawning, possession, modifiers, destruction, traces.
    uintptr_t btClass = Class("OLBTBehaviorTree", g_g.objectClass);
    off.behaviorTree = 0x908;
    off.modifiers = 0x320;  // Pawn.Modifiers from fake::Build
    Prop(enemyPawn, "BehaviorTree", "ObjectProperty", off.behaviorTree, ip, btClass);
    Function(olPc, "Spawn", 0x401, &N_Spawn,
             {{"SpawnClass", "ClassProperty", ip, g_g.classClass},
              {"SpawnOwner", "ObjectProperty", ip, g_g.actor},
              {"SpawnTag", "NameProperty", 8, 0},
              {"SpawnLocation", "StructProperty", 12, vecS},
              {"SpawnRotation", "StructProperty", 12, rotS},
              {"ActorTemplate", "ObjectProperty", ip, g_g.actor},
              {"bNoCollisionFail", "BoolProperty", 4, 1},
              {"ReturnValue", "ObjectProperty", ip, g_g.actor}});
    Function(g_g.controller, "Possess", 0x800, &N_Possess,
             {{"inPawn", "ObjectProperty", ip, g_g.pawn}, {"bVehicleTransition", "BoolProperty", 4, 1}});
    Function(enemyPawn, "ApplyModifiers", 0x800, &N_ApplyModifiers,
             {{"NewModifiers", "StructProperty", 8, g_g.modifiersStruct}});
    Function(enemyPawn, "InitContextualVO", 0x400, &NativeNop, {});
    Function(olPc, "Trace", 0x401, &N_Trace,
             {{"HitLocation", "StructProperty", 12, vecS}, {"HitNormal", "StructProperty", 12, vecS},
              {"TraceEnd", "StructProperty", 12, vecS}, {"TraceStart", "StructProperty", 12, vecS},
              {"bTraceActors", "BoolProperty", 4, 1}, {"ReturnValue", "ObjectProperty", ip, g_g.actor}});
    for (uintptr_t obj : w.Objects())
        if (obj && Get<uintptr_t>(obj + g_cfg.objClass) == g_g.functionClass &&
            Get<uintptr_t>(obj + g_cfg.objOuter) == g_g.actor && Get<int32_t>(obj + g_cfg.objName) == w.Name("Destroy"))
            Put<uintptr_t>(obj + g_cfg.funcFunc(), reinterpret_cast<uintptr_t>(&N_Destroy));
    // Character state.
    off.limping = 0x840;
    off.walkStyle = 0x844;
    Prop(heroClass, "bLimping", "BoolProperty", off.limping, 4, 1);
    Prop(heroClass, "ForcedWalkingStyle", "ByteProperty", off.walkStyle, 1);
    // A pick-up base class so the random teleport has somewhere to go.
    uintptr_t pickable = Class("OLPickableObject", g_g.actor);
    Put<uintptr_t>(batteryClass + g_cfg.structSuper(), pickable);
    o.soldierClass = soldier;
    o.botClass = botClass;
    o.sightClass = sightClass;
    o.cylClass = cylClass;
    o.skelCompClass = skelCompClass;

    // --- Instances ---
    const size_t big = 0x1000;
    o.engine = w.Object("OLEngine_0", olEngine, g_g.engine, big);
    o.viewport = w.Object("OLGameViewportClient_0", olVp, o.engine, big);
    o.localPlayer = w.Object("LocalPlayer_0", lpClass, o.engine, big);
    uintptr_t level = w.Object("PersistentLevel", g_g.objectClass, g_g.engine, big);
    o.level = level;
    uintptr_t aiPackage = w.Object("02_AI_Behaviors", g_g.packageClass, 0, 0);
    o.soldierBT = w.Object("Soldier_BT", btClass, aiPackage, 0x200);
    o.worldInfo = w.Object("WorldInfo_0", worldInfoClass, level, big);
    o.game = w.Object("OLGame_0", olGameClass, level, big);
    o.pc = w.Object("OLPlayerController_0", olPc, level, big);
    o.hero = w.Object("OLHero_0", heroClass, level, big);
    o.heroMesh = w.Object("SkeletalMeshComponent_0", skelCompClass, o.hero, big);
    o.hud = w.Object("OLHUD_0", hudClass, level, big);
    o.menuManager = w.Object("OLUIFrontEnd_0", frontEnd, o.hud, big);
    o.optionsView = w.Object("OLUIFrontEnd_Options_0", optionsClass, o.menuManager, big);
    o.cheat = w.Object("OLCheatManager_0", olCheat, o.pc, big);
    uintptr_t pri = w.Object("PlayerReplicationInfo_0", priClass, level, big);
    o.enemy = w.Object("OLEnemySoldier_0", soldier, level, big);
    o.enemyArchetype = w.Object("Soldier_Archetype", soldier, g_g.engine, big);
    o.bot = w.Object("OLBot_0", botClass, level, big);
    o.sight = w.Object("OLAISightComponent_0", sightClass, o.bot, big);
    o.cylinder = w.Object("CylinderComponent_0", cylClass, o.enemy, big);
    o.enemyMesh = w.Object("SkeletalMeshComponent_1", skelCompClass, o.enemy, big);
    o.battery = w.Object("OLBatteriesPickupFactory_0", batteryClass, level, big);
    o.soldierMesh = w.Object("Soldier-03", skelMeshClass, g_g.engine, big);
    o.priestMesh = w.Object("Priest-01", skelMeshClass, g_g.engine, big);
    o.fingerlessMesh = w.Object("Miles_Fingerless", skelMeshClass, g_g.engine, big);
    o.normalMesh = w.Object("Miles", skelMeshClass, g_g.engine, big);
    w.Name("FakeMap_P");
    int32_t cpName = w.Name("Admin_Gates");
    w.Name("CustomEnemy");

    // Wire them up.
    Put<uintptr_t>(o.engine + 0x500, o.viewport);
    uintptr_t players = w.Alloc(4 * P);
    Put<uintptr_t>(players, o.localPlayer);
    Put<uintptr_t>(o.engine + 0x508, players);
    Put<int32_t>(o.engine + 0x508 + ip, 1);
    Put<int32_t>(o.engine + 0x508 + ip + 4, 4);
    Put<uintptr_t>(o.localPlayer + 0x500, o.pc);
    for (uintptr_t a : {o.worldInfo, o.game, o.pc, o.hero, o.hud, pri, o.enemy, o.bot, o.battery, o.cheat})
        Put<uintptr_t>(a + off.worldInfo, o.worldInfo);
    Put<float>(o.worldInfo + off.timeDilation, 1.f);
    Put<float>(o.worldInfo + off.gravity, -750.f);
    Put<uintptr_t>(o.worldInfo + off.wiGame, o.game);
    Put<int32_t>(o.game + off.checkpoint, cpName);
    Put<uintptr_t>(o.pc + off.pcHero, o.hero);
    Put<uintptr_t>(o.pc + off.pawnController, o.hero);  // Controller.Pawn
    Put<uintptr_t>(o.pc + off.pcHud, o.hud);
    Put<uintptr_t>(o.pc + off.pri, pri);
    Put<int32_t>(o.pc + off.numBatt, 2);
    Put<int32_t>(o.pc + off.maxBatt, 10);
    Put<uint32_t>(o.pc + off.debugFlags, 8u);  // has the camcorder
    Put<int32_t>(o.hero + off.health, 100);
    Put<int32_t>(o.hero + off.healthMax, 100);
    Put<float>(o.hero + off.walk, 200.f);
    Put<float>(o.hero + off.run, 450.f);
    Put<float>(o.hero + off.crouch, 75.f);
    Put<float>(o.hero + off.battEnergy, 0.5f);
    Put<float>(o.hero + off.battDuration, 150.f);
    Put<float>(o.hero + off.defFov, 90.f);
    Put<float>(o.hero + off.customTD, 1.f);
    Put<omm::ue3::FVector>(o.hero + off.location, omm::ue3::FVector(1000, 2000, 300));
    Put<uintptr_t>(o.hero + off.mesh, o.heroMesh);
    Put<uintptr_t>(o.heroMesh + off.skelMesh, o.normalMesh);
    Put<uintptr_t>(o.hero + off.fingerless, o.fingerlessMesh);
    Put<uintptr_t>(o.hud + off.hudMenu, o.menuManager);
    Put<uintptr_t>(o.enemy + off.pawnController, o.bot);
    Put<uintptr_t>(o.bot + off.pawnController, o.enemy);  // Controller.Pawn
    Put<uintptr_t>(o.bot + off.botSight, o.sight);
    Put<uint8_t>(o.bot + off.botState, 3);  // chasing
    Put<int32_t>(o.enemy + off.health, 100);
    Put<float>(o.enemy + off.customTD, 1.f);
    Put<float>(o.bot + off.customTD, 1.f);
    Put<float>(o.enemy + off.hearing, 2000.f);
    Put<float>(o.enemy + off.enemyDamage, 21.f);
    Put<omm::ue3::FVector>(o.enemy + off.location, omm::ue3::FVector(1500, 2000, 300));
    Put<uintptr_t>(o.enemy + off.cylinder, o.cylinder);
    Put<float>(o.cylinder + off.collisionHeight, 88.f);
    Put<uintptr_t>(o.enemy + off.mesh, o.enemyMesh);
    Put<uintptr_t>(o.enemyMesh + off.skelMesh, o.soldierMesh);
    Put<uintptr_t>(o.enemy + off.behaviorTree, o.soldierBT);
    Put<omm::ue3::FVector>(o.battery + off.location, omm::ue3::FVector(1200, 2100, 300));
    Put<int32_t>(o.battery + off.battCount, 2);

    // Real vtables: ProcessEvent at slot 67, everything else a no-op.
    for (uintptr_t& e : g_vtable) e = reinterpret_cast<uintptr_t>(&DummyVirtual);
    g_vtable[67] = reinterpret_cast<uintptr_t>(&FakeProcessEvent);
    for (uintptr_t obj : w.Objects())
        if (obj) Put<uintptr_t>(obj, reinterpret_cast<uintptr_t>(g_vtable));

    w.Publish(0x1800, 0x3400);
}

// --- Driving the game loop ---------------------------------------------------------------
uint64_t g_frame = 0;

void Frame() {
    uintptr_t canvas = 0;
    uint8_t parms[16] = {};
    std::memcpy(parms, &canvas, P);
    FakeProcessEvent(reinterpret_cast<void*>(o.viewport), reinterpret_cast<void*>(o.postRender), parms, nullptr);
    ++g_frame;
    Sleep(2);
}

void Frames(int n) {
    for (int i = 0; i < n; ++i) Frame();
}

bool RunUntil(const std::function<bool()>& cond, int maxFrames) {
    for (int i = 0; i < maxFrames; ++i) {
        if (cond()) return true;
        Frame();
    }
    return cond();
}

omm::game::ModState& Ui() { return omm::game::state::Ui(); }
void Publish() {
    omm::game::state::Publish();
    Frames(3);
}

bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

// The menu without a graphics device: Dear ImGui builds its draw lists all
// the same, which runs every page's code.
void UiFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    omm::ui::Frame();
    ImGui::Render();
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Build();
    std::printf("[harness] %zu-bit, %zu fake objects, ProcessEvent at %p\n", P * 8, g_w->Objects().size(),
                reinterpret_cast<void*>(&FakeProcessEvent));

    // Start the mod exactly like DllMain does.
    HANDLE t = CreateThread(nullptr, 0, &omm::app::InitThread, GetModuleHandleW(nullptr), 0, nullptr);
    EXPECT(t != nullptr);

    // 1. Engine found, frame hook installed, first frames processed.
    bool hooked = RunUntil([] { return Get<uintptr_t>(o.postRender + g_cfg.funcFunc()) != reinterpret_cast<uintptr_t>(&ScriptBody); }, 6000);
    EXPECT(hooked);
    std::printf("[harness] frame hook %s after %llu frames\n", hooked ? "installed" : "NOT installed",
                static_cast<unsigned long long>(g_frame));
    if (!hooked) {
        for (const std::string& l : omm::log::Recent(60)) std::printf("    log: %s\n", l.c_str());
        return 1;
    }
    bool ready = RunUntil([] { return omm::game::state::GetSnapshot().engineReady; }, 200);
    EXPECT(ready);
    EXPECT(omm::ue3::ProcessEventReady());
    EXPECT(omm::ue3::ProcessEventIndex() == 67);
    std::printf("[harness] ProcessEvent discovered at vtable slot %d\n", omm::ue3::ProcessEventIndex());

    // 2. Snapshot built through real ProcessEvent calls (out params, string returns).
    Frames(5);
    omm::game::Snapshot snap = omm::game::state::GetSnapshot();
    EXPECT(snap.inGame);
    EXPECT(snap.mapName == "FakeMap_P");
    EXPECT(snap.checkpoint == "Admin_Gates");
    EXPECT(Near(snap.camFov, 90.f));
    EXPECT(Near(snap.camLocation.Z, 364.f) && snap.camRotation.Yaw == 16384);
    EXPECT(snap.health == 100 && snap.healthMax == 100);
    EXPECT(snap.batteries == 2 && snap.maxBatteries == 10);
    EXPECT(snap.hasCamcorder);
    EXPECT(snap.enemies.size() == 1);  // the archetype is not a world actor
    if (!snap.enemies.empty()) {
        EXPECT(snap.enemies[0].displayName == "Chris Walker");
        EXPECT(snap.enemies[0].state == "Chasing");
    }

    // 3. Options: god mode on and off.
    Ui().godMode = true;
    Publish();
    EXPECT(Get<uint32_t>(o.pc + off.godFlags) & 1u);
    Ui().godMode = false;
    Publish();
    EXPECT(!(Get<uint32_t>(o.pc + off.godFlags) & 1u));

    // 4. Movement speed editor, restored when turned off.
    Ui().speedOverride = true;
    Ui().speedMultiplier = 2.f;
    Publish();
    EXPECT(Near(Get<float>(o.hero + off.walk), 400.f) && Near(Get<float>(o.hero + off.run), 900.f));
    Ui().speedOverride = false;
    Publish();
    EXPECT(Near(Get<float>(o.hero + off.walk), 200.f) && Near(Get<float>(o.hero + off.run), 450.f));

    // 5. Unlimited batteries creates/enables the cheat manager (AddCheats).
    Ui().unlimitedBatteries = true;
    Publish();
    EXPECT(Get<uintptr_t>(o.pc + off.pcCheat) == o.cheat);
    EXPECT((Get<uint32_t>(o.cheat + off.cheatFlags) & 3u) == 3u);
    EXPECT(Near(Get<float>(o.hero + off.battEnergy), 1.f));
    Ui().unlimitedBatteries = false;
    Publish();
    EXPECT(!(Get<uint32_t>(o.cheat + off.cheatFlags) & 2u));

    // 6. Invisibility.
    Ui().invisible = true;
    Publish();
    EXPECT(Get<uint32_t>(o.hero + off.bIsGhost) & 1u);
    EXPECT(Get<uint32_t>(o.sight + off.sightIgnore) & 1u);
    EXPECT(Near(Get<float>(o.enemy + off.hearing), 0.f));
    Ui().invisible = false;
    Publish();
    EXPECT(!(Get<uint32_t>(o.hero + off.bIsGhost) & 1u));
    EXPECT(Near(Get<float>(o.enemy + off.hearing), 2000.f));

    // 7. Game speed and frozen enemies.
    Ui().gameSpeedOverride = true;
    Ui().gameSpeed = 0.5f;
    Ui().freezeEnemies = true;
    Publish();
    EXPECT(Near(Get<float>(o.worldInfo + off.timeDilation), 0.5f));
    EXPECT(Get<float>(o.enemy + off.customTD) < 0.01f);
    Ui().gameSpeedOverride = false;
    Ui().freezeEnemies = false;
    Publish();
    EXPECT(Near(Get<float>(o.worldInfo + off.timeDilation), 1.f));
    EXPECT(Near(Get<float>(o.enemy + off.customTD), 1.f));

    // 8. Noclip (the game's Ghost cheat) and teleporting.
    omm::game::Enqueue([] { omm::game::actions::ToggleNoclip(); });
    Frames(3);
    EXPECT(Get<uint32_t>(o.pc + off.debugFlags) & 1u);
    EXPECT(omm::game::state::GetSnapshot().ghost);
    omm::game::Enqueue([] { omm::game::TeleportPlayer(omm::ue3::FVector(10, 20, 30), nullptr); });
    Frames(2);
    EXPECT(Near(Get<omm::ue3::FVector>(o.pc + off.camPos).Z, 30.f));  // flying: the camera moves
    omm::game::Enqueue([] { omm::game::actions::ToggleNoclip(); });
    omm::game::Enqueue([] { omm::game::TeleportPlayer(omm::ue3::FVector(111, 222, 333), nullptr); });
    Frames(3);
    EXPECT(!(Get<uint32_t>(o.pc + off.debugFlags) & 1u));
    omm::ue3::FVector heroLoc = Get<omm::ue3::FVector>(o.hero + off.location);
    EXPECT(Near(heroLoc.X, 111.f) && Near(heroLoc.Y, 222.f) && Near(heroLoc.Z, 333.f));

    // 9. Console commands and checkpoints (string parameters and returns).
    omm::game::Enqueue([] { omm::game::actions::RunConsoleCommand("stat fps"); });
    Frames(2);
    EXPECT(g_lastConsole == "stat fps");
    std::vector<std::string> console = omm::game::actions::ConsoleLog();
    EXPECT(!console.empty() && console.back() == "ok: stat fps");
    omm::game::Enqueue([] { omm::game::actions::LoadCheckpoint("Prison_Start"); });
    Frames(2);
    EXPECT(g_lastCheckpoint == "Prison_Start");

    // 10. ESP finds the battery and the enemy.
    Ui().espEnabled = true;
    Publish();
    Frames(5);
    snap = omm::game::state::GetSnapshot();
    bool battery = false, enemy = false;
    for (const omm::game::EspItem& it : snap.esp) {
        if (it.category == omm::game::EspCategory::Battery && it.label == "Batteries x2") battery = true;
        if (it.category == omm::game::EspCategory::Enemy && it.label == "Chris Walker" && Near(it.height, 88.f))
            enemy = true;
    }
    EXPECT(battery);
    EXPECT(enemy);
    Ui().espEnabled = false;
    Publish();

    // 11. The game's Options screen is recognised (for the MOD MENU button).
    uintptr_t stack = g_w->Alloc(4 * P);
    Put<uintptr_t>(stack, o.optionsView);
    Put<uintptr_t>(o.menuManager + off.viewStack, stack);
    Put<int32_t>(o.menuManager + off.viewStack + static_cast<int32_t>(P), 1);
    Put<uint32_t>(o.menuManager + off.menuOpen, 1u);
    Put<uint8_t>(o.menuManager + off.menuType, 1);
    Frames(2);
    EXPECT(omm::game::state::GetSnapshot().menu == omm::game::GameMenu::Options);
    Put<uint32_t>(o.menuManager + off.menuOpen, 0u);
    Frames(2);
    EXPECT(omm::game::state::GetSnapshot().menu == omm::game::GameMenu::None);

    // 12. Model swapper: Chris Walker gets Father Martin's model, then back.
    omm::game::enemies::SetModelRule("OLEnemySoldier", "Engine.Priest-01");
    Frames(3);
    EXPECT(Get<uintptr_t>(o.enemyMesh + off.skelMesh) == o.priestMesh);
    omm::game::enemies::ClearModelRules();
    Frames(3);
    EXPECT(Get<uintptr_t>(o.enemyMesh + off.skelMesh) == o.soldierMesh);

    // 13. Outfits: fingerless hands, then the original model again.
    omm::game::Enqueue([] { omm::game::actions::SetOutfit(omm::game::actions::Outfit::Fingerless); });
    Frames(2);
    EXPECT(Get<uintptr_t>(o.heroMesh + off.skelMesh) == o.fingerlessMesh);
    omm::game::Enqueue([] { omm::game::actions::SetOutfit(omm::game::actions::Outfit::Original); });
    Frames(2);
    EXPECT(Get<uintptr_t>(o.heroMesh + off.skelMesh) == o.normalMesh);

    // 14. Plugin API on the game thread.
    static int pluginHealth = -1;
    const OMM_Api* api = OMM_GetApi();
    EXPECT(api && api->version == OMM_API_VERSION);
    api->QueueOnGameThread(
        [](void*) {
            const OMM_Api* a = OMM_GetApi();
            int32_t hp = 0;
            if (a->GetInt(a->Hero(), "Health", &hp)) pluginHealth = hp;
        },
        nullptr);
    Frames(2);
    EXPECT(pluginHealth == 100);

    // 15. A crash inside the game is caught: the game keeps running.
    uint32_t faultsBefore = omm::guard::FaultCount();
    omm::game::Enqueue([] { omm::ue3::CallNoArgs(omm::game::W().pc, "CrashTest"); });
    Frames(3);
    EXPECT(g_crashCalls == 1);
    EXPECT(omm::guard::FaultCount() == faultsBefore + 1);
    uint64_t framesBefore = omm::game::state::GetSnapshot().frame;
    Frames(5);
    EXPECT(omm::game::state::GetSnapshot().frame > framesBefore);  // still running
    EXPECT(omm::guard::LastFault().find("menu action") != std::string::npos);

    // 16. Performance mode issues the engine's scalability commands.
    Ui().perfLevel = 1;
    Publish();
    EXPECT(g_lastConsole.rfind("scale set ", 0) == 0);
    Ui().perfLevel = 0;
    Publish();

    // 17. Spawning an extra Chris Walker (spawn, possess, behaviour tree, modifiers), then removing it.
    omm::game::enemies::SpawnRequest req;
    req.type = 0;
    req.weapon = 2;
    omm::game::Enqueue([req] { omm::game::enemies::Spawn(req); });
    Frames(3);
    snap = omm::game::state::GetSnapshot();
    EXPECT(snap.modSpawnedEnemies == 1);
    EXPECT(snap.enemies.size() == 2);
    uintptr_t spawned = 0;
    for (const omm::game::EnemyInfo& e : snap.enemies)
        if (e.spawnedByMod) spawned = reinterpret_cast<uintptr_t>(omm::ue3::ObjectAt(e.index));
    EXPECT(spawned != 0);
    if (spawned) {
        EXPECT(Get<uintptr_t>(spawned + off.behaviorTree) == o.soldierBT);           // copied from the template
        EXPECT(Get<uintptr_t>(Get<uintptr_t>(spawned + off.mesh) + off.skelMesh) == o.soldierMesh);
        uintptr_t bot = Get<uintptr_t>(spawned + off.pawnController);
        EXPECT(bot && Get<uintptr_t>(bot + off.pawnController) == spawned);           // possessed
        EXPECT(Get<uint8_t>(spawned + off.modifiers + 4) == 2);                         // weapon applied
        EXPECT(Get<uint32_t>(spawned + off.modifiers) & 2u);                             // bShouldAttack
    }
    omm::game::Enqueue([] { omm::game::enemies::KillAll(true); });
    Frames(3);
    snap = omm::game::state::GetSnapshot();
    EXPECT(snap.modSpawnedEnemies == 0);
    EXPECT(snap.enemies.size() == 1);

    // 18. Horde mode brings enemies on its own.
    Ui().hordeMode = true;
    Ui().hordeInterval = 5.f;
    Ui().hordeMaxEnemies = 1;
    Ui().hordeEnemyType = 0;
    Publish();
    RunUntil([] { return omm::game::state::GetSnapshot().modSpawnedEnemies >= 1; }, 2500);
    EXPECT(omm::game::state::GetSnapshot().modSpawnedEnemies == 1);
    Ui().hordeMode = false;
    Publish();
    omm::game::Enqueue([] { omm::game::enemies::KillAll(true); });
    Frames(3);

    // 19. Random teleport, teleport to crosshair, character preset.
    omm::game::Enqueue([] { omm::game::actions::TeleportRandomLocation(); });
    Frames(2);
    heroLoc = Get<omm::ue3::FVector>(o.hero + off.location);
    EXPECT(Near(heroLoc.X, 1200.f) && Near(heroLoc.Y, 2100.f));  // the only spot: the battery pick-up
    omm::game::Enqueue([] { omm::game::actions::TeleportToCrosshair(); });
    Frames(2);
    heroLoc = Get<omm::ue3::FVector>(o.hero + off.location);
    EXPECT(Near(heroLoc.X, 1800.f) && Near(heroLoc.Z, 300.f + 60.f + 50.f));
    omm::game::actions::ApplyCharacterPreset(1);  // "Limping"
    Frames(3);
    EXPECT(Get<uint32_t>(o.hero + off.limping) & 1u);
    EXPECT(Get<uint8_t>(o.hero + off.walkStyle) == 2);
    omm::game::actions::ApplyCharacterPreset(0);
    Frames(3);
    EXPECT(!(Get<uint32_t>(o.hero + off.limping) & 1u));

    // 20. Object inspector.
    omm::game::inspector::InspectObject("Hero", "");
    Frames(2);
    omm::game::inspector::Result ir = omm::game::inspector::Last();
    EXPECT(ir.title.find("OLHero_0") != std::string::npos && ir.lines.size() > 10);

    // 21. The whole menu, every page, headless.
    ImGui::CreateContext();
    {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefault();
        unsigned char* px = nullptr;
        int fw = 0, fh = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
    }
    omm::ui::Setup();
    Ui().espEnabled = true;
    omm::game::state::Publish();
    Frames(3);
    UiFrame();
    EXPECT(!omm::ui::IsMenuOpen());
    EXPECT(omm::ui::OnHotkey(0x2D, true, false));  // Insert
    UiFrame();
    EXPECT(omm::ui::IsMenuOpen());
    int pagesDrawn = 0;
    for (int i = 0; i < omm::ui::PageCount(); ++i) {
        omm::ui::SelectPage(i);
        UiFrame();
        UiFrame();
        Frames(1);
        ++pagesDrawn;
    }
    EXPECT(pagesDrawn == omm::ui::PageCount());
    // The game's Options screen with the menu closed: the MOD MENU button.
    omm::ui::OpenMenu(false);
    Put<uint32_t>(o.menuManager + off.menuOpen, 1u);
    Frames(2);
    UiFrame();
    EXPECT(omm::game::state::GetSnapshot().menu == omm::game::GameMenu::Options);
    Put<uint32_t>(o.menuManager + off.menuOpen, 0u);
    Frames(2);
    UiFrame();
    ImGui::DestroyContext();

    std::printf("[harness] %d checks, %d failures, %u recovered fault(s), %llu frames\n", g_checks, g_failures,
                omm::guard::FaultCount(), static_cast<unsigned long long>(g_frame));
    if (g_failures)
        for (const std::string& l : omm::log::Recent(80)) std::printf("    log: %s\n", l.c_str());
    std::fflush(stdout);
    // The mod's threads run forever (like in the game); end the process.
    ExitProcess(g_failures ? 1 : 0);
}
