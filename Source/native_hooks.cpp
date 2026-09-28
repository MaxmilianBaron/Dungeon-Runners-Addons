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
void* MoveUiFrameOriginal = nullptr;
volatile uintptr_t MoveUiFrameTarget = 0;
void* MoveControlOriginal = nullptr;
uintptr_t MoveControlContinue = 0;
volatile uintptr_t MoveControlTargets[6] = {};
void* MoveProjectionOriginal = nullptr;
volatile uintptr_t MoveProjectionTarget = 0;
void* MythicDropOriginal = nullptr;
void* MythicInventoryOriginal = nullptr;
void* WellAcceptedOriginal = nullptr;
void* WellFinalizedOriginal = nullptr;
void* WellLoginOriginal = nullptr;
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
METER_STUB(WellAcceptedHook, WellAcceptedOriginal, 10)
METER_STUB(WellFinalizedHook, WellFinalizedOriginal, 11)
METER_STUB(WellLoginHook, WellLoginOriginal, 12)
METER_STUB(MoveUiFrameDispatch, MoveUiFrameOriginal, 13)

extern "C" __declspec(naked) void MoveControlHook() {
    __asm pushfd
    __asm cmp esi, dword ptr [MoveControlTargets]
    __asm je scaled
    __asm cmp esi, dword ptr [MoveControlTargets+4]
    __asm je scaled
    __asm cmp esi, dword ptr [MoveControlTargets+8]
    __asm je scaled
    __asm cmp esi, dword ptr [MoveControlTargets+12]
    __asm je scaled
    __asm cmp esi, dword ptr [MoveControlTargets+16]
    __asm je scaled
    __asm cmp esi, dword ptr [MoveControlTargets+20]
    __asm je scaled
    __asm popfd
    __asm jmp dword ptr [MoveControlOriginal]
    __asm scaled:
    __asm popfd
    __asm mov edx,eax
    __asm push ebp
    __asm mov ecx,esi
    __asm call MoveControlDraw
    __asm jmp dword ptr [MoveControlContinue]
}

extern "C" __declspec(naked) void MoveProjectionHook() {
    __asm pushfd
    __asm cmp ecx, dword ptr [MoveProjectionTarget]
    __asm jne original
    __asm popfd
    __asm jmp MoveProjectionLoad
    __asm original:
    __asm popfd
    __asm jmp dword ptr [MoveProjectionOriginal]
}

extern "C" __declspec(naked) void MoveUiFrameHook() {
    __asm pushfd
    __asm cmp ecx, dword ptr [MoveUiFrameTarget]
    __asm jne original
    __asm popfd
    __asm jmp MoveUiFrameDispatch
    __asm original:
    __asm popfd
    __asm jmp dword ptr [MoveUiFrameOriginal]
}

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
