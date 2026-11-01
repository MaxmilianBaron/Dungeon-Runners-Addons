#pragma once
#include <windows.h>
#include <cstdint>

constexpr uint32_t DungeonRunnersAddonsApiVersion = 1;
using DungeonRunnersAddonsInitializeProc = BOOL (WINAPI*)(uint32_t);
