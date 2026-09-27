#pragma once
#include <windows.h>
#include <cstdint>

constexpr uint32_t ExtensionApiVersion = 1;

struct ExtensionHost {
    uint32_t size;
    uint32_t version;
    const wchar_t* gameDirectory;
    const wchar_t* addonDirectory;
};

struct ExtensionOption {
    char label[32];
    char help[256];
};

struct ExtensionDefinition {
    uint32_t size;
    uint32_t version;
    char id[48];
    char name[48];
    char description[192];
    uint32_t optionCount;
    ExtensionOption options[4];
    void* context;
    int (WINAPI* read)(void*,uint32_t);
    BOOL (WINAPI* apply)(void*,const int*,uint32_t);
};

using ExtensionInitialize = BOOL (WINAPI*)(const ExtensionHost*,ExtensionDefinition*);

constexpr uint32_t ExtensionSettingsVersion = 1;
constexpr uint32_t ExtensionSettingsLimit = 24;
enum class ExtensionValueFormat : uint32_t { Toggle, Integer, Percent, TenthsSeconds };
struct ExtensionSetting {
    char label[32];
    char help[256];
    uint32_t page;
    ExtensionValueFormat format;
    int minimum,maximum,step,initial;
};
struct ExtensionSettings {
    uint32_t size,version,pageCount,optionCount;
    char pages[4][24];
    ExtensionSetting options[ExtensionSettingsLimit];
    void* context;
    int (WINAPI* read)(void*,uint32_t);
    BOOL (WINAPI* apply)(void*,const int*,uint32_t,char*,uint32_t);
};
using ExtensionGetSettings = BOOL (WINAPI*)(ExtensionSettings*);
