#pragma once
#include <cstdint>
#include "character_sheet.h"

struct HookRegisters {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax, flags;
};

extern "C" void __cdecl MeterDispatch(unsigned, const HookRegisters*);
extern "C" void MeterBefore();
extern "C" void MeterCommit();
extern "C" void MeterLoading();
extern "C" void MeterValidate();
extern "C" void MeterPresent();
extern "C" void MeterResources();
extern "C" void MeterUiBegin();
extern "C" void MeterUiControl();
extern "C" void MoveUiFrameHook();
extern "C" void* MoveUiFrameOriginal;
extern "C" volatile uintptr_t MoveUiFrameTarget;
extern "C" void MoveControlHook();
extern "C" void* MoveControlOriginal;
extern "C" uintptr_t MoveControlContinue;
extern "C" volatile uintptr_t MoveControlTargets[6];
void __fastcall MoveControlDraw(uintptr_t,uintptr_t,uintptr_t);
extern "C" void MoveProjectionHook();
extern "C" void* MoveProjectionOriginal;
extern "C" volatile uintptr_t MoveProjectionTarget;
extern "C" void __fastcall MoveProjectionLoad(uintptr_t,uintptr_t,unsigned,const float*);
extern "C" void* MeterBeforeOriginal;
extern "C" void* MeterCommitOriginal;
extern "C" void* MeterLoadingOriginal;
extern "C" void* MeterValidateOriginal;
extern "C" void* MeterPresentOriginal;
extern "C" void* MeterResourcesOriginal;
extern "C" void* MeterUiBeginOriginal;
extern "C" void* MeterUiControlOriginal;
extern "C" void MythicDropHook();
extern "C" void MythicInventoryHook();
extern "C" void* MythicDropOriginal;
extern "C" void* MythicInventoryOriginal;
extern "C" void WellAcceptedHook();
extern "C" void WellFinalizedHook();
extern "C" void WellLoginHook();
extern "C" void* WellAcceptedOriginal;
extern "C" void* WellFinalizedOriginal;
extern "C" void* WellLoginOriginal;
extern "C" void CharacterSheetVisualHook();
extern "C" void* CharacterSheetVisualOriginal;
extern "C" volatile uintptr_t CharacterSheetVisualTarget;
extern "C" void __fastcall CharacterSheetVisualDraw(uintptr_t,uintptr_t,uintptr_t,SheetQuad);
extern "C" void LootLabelHook();
extern "C" void* LootLabelOriginal;
extern "C" uintptr_t __cdecl LootLabelDispatch(const HookRegisters*);
extern "C" uintptr_t __cdecl NameplateDispatch(unsigned,const HookRegisters*);
extern "C" void NameplateOptionsHook();
extern "C" void NameplateCreateHook();
extern "C" void NameplateBarsHook();
extern "C" void NameplateNameHook();
extern "C" void NameplatePosseHook();
extern "C" void NameplateRetireHook();
extern "C" void* NameplateOptionsOriginal;
extern "C" void* NameplateCreateOriginal;
extern "C" void* NameplateBarsOriginal;
extern "C" void* NameplateNameOriginal;
extern "C" void* NameplatePosseOriginal;
extern "C" void* NameplateRetireOriginal;
