// OMMInjector - loads OutlastModMenu.dll into a running OLGame.exe.
//
// Only needed if you don't want to use the dinput8.dll method. Usage:
//   OMMInjector.exe                 (waits for OLGame.exe, injects the DLL next to this exe)
//   OMMInjector.exe <path-to-dll>
// Use the 64-bit injector with the 64-bit game and the 32-bit one with the
// 32-bit game.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

DWORD FindProcess(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, exe) == 0) {
            pid = pe.th32ProcessID;
            break;
        }
    }
    CloseHandle(snap);
    return pid;
}

bool IsSameBitness(HANDLE process) {
    BOOL targetWow = FALSE, selfWow = FALSE;
    IsWow64Process(process, &targetWow);
    IsWow64Process(GetCurrentProcess(), &selfWow);
    return targetWow == selfWow;
}

std::wstring DefaultDllPath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = 0;
    return std::wstring(path) + L"OutlastModMenu.dll";
}

int Fail(const char* what) {
    std::printf("Error: %s (code %lu)\n", what, GetLastError());
    std::printf("Press Enter to exit.");
    std::getchar();
    return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring dll = argc > 1 ? argv[1] : DefaultDllPath();
    wchar_t full[MAX_PATH] = {};
    if (!GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr) || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
        std::wprintf(L"Cannot find %ls\n", dll.c_str());
        return Fail("DLL not found");
    }
    std::wprintf(L"Outlast Mod Menu injector\nDLL: %ls\nWaiting for OLGame.exe...\n", full);
    DWORD pid = 0;
    while (!(pid = FindProcess(L"OLGame.exe"))) Sleep(500);
    std::printf("Found OLGame.exe (pid %lu)\n", pid);
    Sleep(2000);  // let the game finish loading its own DLLs

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) return Fail("could not open the game process (try running as administrator)");
    if (!IsSameBitness(process)) {
        CloseHandle(process);
        std::printf("The game and this injector are different builds (32-bit vs 64-bit).\n"
                    "Use the injector from the Win32 folder for the 32-bit game and Win64 for the 64-bit game.\n");
        return Fail("architecture mismatch");
    }
    size_t bytes = (wcslen(full) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return Fail("VirtualAllocEx failed");
    if (!WriteProcessMemory(process, remote, full, bytes, nullptr)) return Fail("WriteProcessMemory failed");
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) return Fail("CreateRemoteThread failed");
    WaitForSingleObject(thread, 15000);
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    if (!code) return Fail("LoadLibrary failed inside the game (see OutlastModMenu.log if it exists)");
    std::printf("Injected. Press INSERT or F1 in the game to open the menu.\n");
    Sleep(2500);
    return 0;
}
