#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cwchar>
#include "addon_api.h"

static INIT_ONCE bootstrap = INIT_ONCE_STATIC_INIT;
static HMODULE graphicsD3D = nullptr;
static HMODULE damageMeter = nullptr;
static IDirect3D9* (WINAPI* createD3D)(UINT) = nullptr;

static bool JoinPath(wchar_t (&result)[MAX_PATH], const wchar_t* directory, const wchar_t* relative) {
    const size_t left = std::wcslen(directory), right = std::wcslen(relative);
    if (!left || left + 1 + right >= MAX_PATH) return false;
    std::wmemcpy(result, directory, left);
    result[left] = L'\\';
    std::wmemcpy(result + left + 1, relative, right + 1);
    return true;
}

static void WriteStatus(const wchar_t* directory, const char* message, DWORD error) {
    wchar_t file[MAX_PATH]{};
    if (!JoinPath(file, directory, L"Addons\\loader.log")) return;
    HANDLE output = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) return;
    char text[256]{};
    const int length = std::snprintf(text, sizeof(text), "%s\nWin32 error: %lu\n", message, error);
    DWORD written = 0;
    if (length > 0 && length < static_cast<int>(sizeof(text))) WriteFile(output, text, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(output);
}

static BOOL CALLBACK InitializeLoader(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, directory, MAX_PATH);
    bool game = false;
    if (length && length < MAX_PATH) {
        if (wchar_t* name = std::wcsrchr(directory, L'\\')) {
            game = _wcsicmp(name + 1, L"DungeonRunners.exe") == 0;
            *name = 0;
        }
    }
    bool chained = false;
    if (game) {
        if (!JoinPath(path, directory, L"d3d9.previous.dll")) return TRUE;
        const DWORD attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
                WriteStatus(directory, "The preserved d3d9 library is not a regular file", ERROR_BAD_PATHNAME);
                return TRUE;
            }
            chained = true;
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            WriteStatus(directory, "Cannot inspect the preserved d3d9 library", GetLastError());
            return TRUE;
        }
    }
    if (!chained) {
        wchar_t system[MAX_PATH]{};
        length = GetSystemDirectoryW(system, MAX_PATH);
        if (!length || length >= MAX_PATH || !JoinPath(path, system, L"d3d9.dll")) return TRUE;
    }
    DWORD previousMode = 0;
    bool modeChanged = SetThreadErrorMode(GetThreadErrorMode() | SEM_FAILCRITICALERRORS, &previousMode) != 0;
    graphicsD3D = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const DWORD graphicsError = GetLastError();
    if (modeChanged) SetThreadErrorMode(previousMode, nullptr);
    if (!graphicsD3D) {
        if (game) WriteStatus(directory, chained ? "Cannot load d3d9.previous.dll; original file was preserved" : "Cannot load system d3d9.dll", graphicsError);
        return TRUE;
    }
    if (chained && GetProcAddress(graphicsD3D, "DungeonRunnersAddonsLoaderVersion")) {
        WriteStatus(directory, "The preserved d3d9 library points to another addon loader", ERROR_INVALID_DATA);
        return TRUE;
    }
    createD3D = reinterpret_cast<decltype(createD3D)>(GetProcAddress(graphicsD3D, "Direct3DCreate9"));
    if (!createD3D) {
        if (game) WriteStatus(directory, "The graphics library has no Direct3DCreate9 export", ERROR_PROC_NOT_FOUND);
        return TRUE;
    }
    if (!game) return TRUE;
    if (!JoinPath(path, directory, L"Addons\\Runtime\\Addons.dll")) {
        WriteStatus(directory, "Addon path is too long; graphics remain available", ERROR_FILENAME_EXCED_RANGE);
        return TRUE;
    }
    modeChanged = SetThreadErrorMode(GetThreadErrorMode() | SEM_FAILCRITICALERRORS, &previousMode) != 0;
    damageMeter = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const DWORD loadError = GetLastError();
    if (modeChanged) SetThreadErrorMode(previousMode, nullptr);
    if (!damageMeter) {
        WriteStatus(directory, "DamageMeter is unavailable; graphics remain available", loadError);
        return TRUE;
    }
    const auto initialize = reinterpret_cast<DungeonRunnersAddonsInitializeProc>(GetProcAddress(damageMeter, "DungeonRunnersAddonsInitialize"));
    if (!initialize) {
        WriteStatus(directory, "DamageMeter has no compatible initialization export; graphics remain available", ERROR_PROC_NOT_FOUND);
        return TRUE;
    }
    if (!initialize(DungeonRunnersAddonsApiVersion)) {
        WriteStatus(directory, "DamageMeter initialization declined; see DamageMeter/status.txt", ERROR_SUCCESS);
        return TRUE;
    }
    WriteStatus(directory, chained ? "Loaded Addons/Runtime/Addons.dll and preserved d3d9.previous.dll" : "Loaded Addons/Runtime/Addons.dll", ERROR_SUCCESS);
    return TRUE;
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT version) {
    static thread_local bool active = false;
    if (active) return nullptr;
    active = true;
    InitOnceExecuteOnce(&bootstrap, InitializeLoader, nullptr, nullptr);
    IDirect3D9* result = createD3D ? createD3D(version) : nullptr;
    active = false;
    return result;
}

extern "C" DWORD WINAPI DungeonRunnersAddonsLoaderVersion() { return 2; }
