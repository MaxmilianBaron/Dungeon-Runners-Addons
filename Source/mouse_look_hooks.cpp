#include "mouse_look_hooks.h"

extern "C" {
void* MouseLookSelectGateway = nullptr;
void* MouseLookClickOriginal = nullptr;
void* MouseLookRightOriginal = nullptr;
uintptr_t MouseLookSelectContinue = 0;
uintptr_t MouseLookSelectExit = 0;
}

static const unsigned MouseLookMxcsr = 0x1f80;

extern "C" __declspec(naked) void MouseLookSelectHook() {
    __asm test edi, edi
    __asm jnz hasMouse
    __asm jmp dword ptr [MouseLookSelectExit]
hasMouse:
    __asm push dword ptr [MouseLookSelectContinue]
    __asm pushfd
    __asm pushad
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0xfffffff0
    __asm fxsave [esp]
    __asm fninit
    __asm ldmxcsr MouseLookMxcsr
    __asm cld
    __asm mov esi, esp
    __asm push ebx
    __asm call MouseLookSelectDispatch
    __asm add esp, 4
    __asm mov [ebx+36], eax
    __asm fxrstor [esi]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}

extern "C" __declspec(naked) void MouseLookClickHook() {
    __asm push dword ptr [MouseLookClickOriginal]
    __asm pushfd
    __asm pushad
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0xfffffff0
    __asm fxsave [esp]
    __asm fninit
    __asm ldmxcsr MouseLookMxcsr
    __asm cld
    __asm mov esi, esp
    __asm push ebx
    __asm call MouseLookClickDispatch
    __asm add esp, 4
    __asm mov [ebx+36], eax
    __asm fxrstor [esi]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}

extern "C" __declspec(naked) void MouseLookRightHook() {
    __asm push dword ptr [MouseLookRightOriginal]
    __asm pushfd
    __asm pushad
    __asm mov ebx, esp
    __asm sub esp, 528
    __asm and esp, 0xfffffff0
    __asm fxsave [esp]
    __asm fninit
    __asm ldmxcsr MouseLookMxcsr
    __asm cld
    __asm mov esi, esp
    __asm push ebx
    __asm call MouseLookRightDispatch
    __asm add esp, 4
    __asm mov [ebx+36], eax
    __asm fxrstor [esi]
    __asm mov esp, ebx
    __asm popad
    __asm popfd
    __asm ret
}

extern "C" __declspec(naked) void MouseLookSkipClick() { __asm ret 16 }
extern "C" __declspec(naked) void MouseLookRightReleased() { __asm xor eax, eax __asm ret }

extern "C" __declspec(naked) void __cdecl MouseLookInvokeSelect(uintptr_t,uintptr_t,uintptr_t) {
    __asm push esi
    __asm push edi
    __asm mov eax, [esp+12]
    __asm mov esi, [esp+16]
    __asm mov edi, [esp+20]
    __asm call eax
    __asm pop edi
    __asm pop esi
    __asm ret
}
