#pragma once
#include "native_hooks.h"

extern "C" void MouseLookSelectHook();
extern "C" void MouseLookClickHook();
extern "C" void MouseLookRightHook();
extern "C" void MouseLookSkipClick();
extern "C" void MouseLookRightReleased();
extern "C" void __cdecl MouseLookInvokeSelect(uintptr_t,uintptr_t,uintptr_t);
extern "C" void* MouseLookSelectGateway;
extern "C" void* MouseLookClickOriginal;
extern "C" void* MouseLookRightOriginal;
extern "C" uintptr_t MouseLookSelectContinue;
extern "C" uintptr_t MouseLookSelectExit;
extern "C" uintptr_t __cdecl MouseLookSelectDispatch(const HookRegisters*);
extern "C" uintptr_t __cdecl MouseLookClickDispatch(const HookRegisters*);
extern "C" uintptr_t __cdecl MouseLookRightDispatch(const HookRegisters*);
