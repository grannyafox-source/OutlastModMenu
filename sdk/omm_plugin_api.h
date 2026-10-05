/*
 * Outlast Mod Menu - plugin API (C ABI).
 *
 * A plugin is a DLL (or .asi) placed in <Outlast>\Binaries\Win64\OutlastModMenu\plugins
 * (Win32 for the 32-bit game). The mod loads every plugin once the engine is
 * ready and calls its exported function:
 *
 *     __declspec(dllexport) int OMM_PluginInit(const OMM_Api* api);   // 0 = success
 *
 * Optional exports:
 *
 *     __declspec(dllexport) const char* OMM_PluginName(void);
 *     __declspec(dllexport) void OMM_PluginShutdown(void);
 *
 * Code that is not loaded by the mod can still reach the API through the
 * mod's own module:
 *
 *     typedef const OMM_Api* (*GetApiFn)(void);
 *     GetApiFn get = (GetApiFn)GetProcAddress(GetModuleHandleA("dinput8.dll"), "OMM_GetApi");
 *
 * Threading: functions marked [game] may only be called on the game thread,
 * i.e. from a frame callback or a function passed to QueueOnGameThread.
 * Objects are opaque pointers to Unreal Engine 3 UObjects; property paths use
 * the game's own names (see the OLGame*.ini files) and may name struct
 * members: "Modifiers.bShouldAttack".
 */
#ifndef OMM_PLUGIN_API_H
#define OMM_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OMM_API_VERSION 1

typedef void (*OMM_Callback)(void* user);

typedef struct OMM_Api {
    uint32_t version; /* OMM_API_VERSION */
    uint32_t size;    /* sizeof(OMM_Api) */

    /* --- General (any thread) --- */
    void (*Log)(const char* utf8);
    void (*Notify)(const char* utf8, float seconds);
    const char* (*ModDirectory)(void); /* ...\OutlastModMenu (UTF-8) */
    const char* (*ModVersion)(void);

    /* --- Callbacks --- */
    /* fn runs on the game thread once per frame. */
    void (*RegisterFrameCallback)(OMM_Callback fn, void* user);
    /* draw runs on the render thread inside the menu's "Plugins" tab. Use
       ImGui through GetImGuiContext (same Dear ImGui version as ImGuiVersion). */
    void (*RegisterMenuSection)(const char* title, OMM_Callback draw, void* user);
    void* (*GetImGuiContext)(void);
    const char* (*ImGuiVersion)(void);
    /* Runs fn once on the game thread at the start of the next frame. */
    void (*QueueOnGameThread)(OMM_Callback fn, void* user);

    /* --- Game objects [game] --- */
    void* (*PlayerController)(void);
    void* (*Hero)(void);
    void* (*WorldInfo)(void);
    void* (*FindObject)(const char* name, const char* className); /* className may be NULL */
    void* (*FindClass)(const char* name);
    int (*IsA)(void* object, const char* className);
    const char* (*ObjectName)(void* object); /* valid until the next call */

    int (*GetFloat)(void* object, const char* path, float* out);
    int (*SetFloat)(void* object, const char* path, float value);
    int (*GetInt)(void* object, const char* path, int32_t* out);
    int (*SetInt)(void* object, const char* path, int32_t value);
    int (*GetBool)(void* object, const char* path, int* out);
    int (*SetBool)(void* object, const char* path, int value);
    void* (*GetObjectProperty)(void* object, const char* path);
    int (*SetObjectProperty)(void* object, const char* path, void* value);

    /* Calls a function that takes no parameters (e.g. "ResetAfterTeleport"). */
    int (*CallNoArgs)(void* object, const char* function);
    /* Runs a console command; returns 1 if it was executed. */
    int (*ConsoleCommand)(const char* utf8);
} OMM_Api;

typedef int (*OMM_PluginInitFn)(const OMM_Api* api);
typedef const char* (*OMM_PluginNameFn)(void);
typedef void (*OMM_PluginShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* OMM_PLUGIN_API_H */
