#include "native_hooks.h"

extern "C" {
void* MeterBeforeOriginal = nullptr;
void* MeterCommitOriginal = nullptr;
void* MeterLoadingOriginal = nullptr;
void* MeterValidateOriginal = nullptr;
void* MeterPresentOriginal = nullptr;
void* MeterResourcesOriginal = nullptr;
void* MeterUiBeginOriginal = nullptr;
void* MeterUiControlOriginal = nullptr;
void* MythicDropOriginal = nullptr;
void* MythicInventoryOriginal = nullptr;
void* CharacterSheetVisualOriginal = nullptr;
volatile uintptr_t CharacterSheetVisualTarget = 0;
void* LootLabelOriginal = nullptr;
void* NameplateOptionsOriginal = nullptr;
void* NameplateCreateOriginal = nullptr;
void* NameplateBarsOriginal = nullptr;
void* NameplateNameOriginal = nullptr;
void* NameplatePosseOriginal = nullptr;
void* NameplateRetireOriginal = nullptr;
}

static const unsigned DefaultMxcsr = 0x1f80;

#define METER_STUB(Name, Original, Number) \
extern "C" __declspec(naked) void Name() { \
    __asm pushfd \
    __asm pushad \
    __asm mov ebx, esp \
    __asm sub esp, 528 \
    __asm and esp, 0xfffffff0 \
    __asm fxsave [esp] \
    __asm fninit \
    __asm ldmxcsr DefaultMxcsr \
    __asm cld \
    __asm mov esi, esp \
    __asm push ebx \
    __asm push Number \
    __asm call MeterDispatch \
    __asm add esp, 8 \
    __asm fxrstor [esi] \
    __asm mov esp, ebx \
    __asm popad \
    __asm popfd \
    __asm jmp dword ptr [Original] \
}

METER_STUB(MeterBefore, MeterBeforeOriginal, 0)
METER_STUB(MeterCommit, MeterCommitOriginal, 1)
METER_STUB(MeterLoading, MeterLoadingOriginal, 2)
METER_STUB(MeterValidate, MeterValidateOriginal, 3)
METER_STUB(MeterPresent, MeterPresentOriginal, 4)
METER_STUB(MeterResources, MeterResourcesOriginal, 5)
METER_STUB(MeterUiBegin, MeterUiBeginOriginal, 6)
METER_STUB(MeterUiControl, MeterUiControlOriginal, 7)
METER_STUB(MythicDropHook, MythicDropOriginal, 8)
METER_STUB(MythicInventoryHook, MythicInventoryOriginal, 9)

extern "C" __declspec(naked) void CharacterSheetVisualHook() {
    __asm cmp ecx, dword ptr [CharacterSheetVisualTarget]
    __asm jne original
    __asm jmp CharacterSheetVisualDraw
    __asm original:
    __asm jmp dword ptr [CharacterSheetVisualOriginal]
}

#define NAMEPLATE_STUB(Name, Original, Number) \
extern "C" __declspec(naked) void Name() { \
    __asm push dword ptr [Original] \
    __asm pushfd \
    __asm pushad \
    __asm mov ebx, esp \
    __asm sub esp, 528 \
    __asm and esp, 0xfffffff0 \
    __asm fxsave [esp] \
    __asm fninit \
    __asm ldmxcsr DefaultMxcsr \
    __asm cld \
    __asm mov esi, esp \
    __asm push ebx \
    __asm push Number \
    __asm call NameplateDispatch \
    __asm add esp, 8 \
    __asm mov [ebx+36], eax \
    __asm fxrstor [esi] \
    __asm mov esp, ebx \
    __asm popad \
    __asm popfd \
    __asm ret \
}

NAMEPLATE_STUB(NameplateOptionsHook,NameplateOptionsOriginal,0)
NAMEPLATE_STUB(NameplateCreateHook,NameplateCreateOriginal,1)
NAMEPLATE_STUB(NameplateBarsHook,NameplateBarsOriginal,2)
NAMEPLATE_STUB(NameplateNameHook,NameplateNameOriginal,3)
NAMEPLATE_STUB(NameplatePosseHook,NameplatePosseOriginal,4)
NAMEPLATE_STUB(NameplateRetireHook,NameplateRetireOriginal,5)

extern "C" __declspec(naked) void LootLabelHook() {
    __asm push dword ptr [LootLabelOriginal]
    __asm pushfd
    __asm pushad
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0xfffffff0
    __asm fxsave [esp]
    __asm fninit
    __asm ldmxcsr DefaultMxcsr
    __asm cld
    __asm mov esi, esp
    __asm push ebx
    __asm call LootLabelDispatch
    __asm add esp, 4
    __asm mov [ebx+36], eax
    __asm fxrstor [esi]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}
