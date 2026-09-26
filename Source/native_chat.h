#pragma once
#include <cstdint>
#include <cstring>

inline bool SendNativePartyLine(uintptr_t entry,uintptr_t client,const char* text) {
    struct TextBlock { uint32_t length; char text[192]; } block{};
    const size_t size = text ? strnlen_s(text,sizeof(block.text)) : 0;
    if (!entry || !client || !size || size >= sizeof(block.text)) return false;
    block.length = static_cast<uint32_t>(size);
    std::memcpy(block.text,text,size);
    TextBlock* nativeString = &block;
    TextBlock** argument = &nativeString;
    __asm {
        push ebx
        mov ebx, client
        push argument
        push 3
        call entry
        pop ebx
    }
    return true;
}

inline bool AddNativeLocalLine(uintptr_t entry,uintptr_t client,const char* text,bool& fault) {
    struct TextBlock { uint32_t length; char text[192]; } block{};
    const size_t size = text ? strnlen_s(text,sizeof(block.text)) : 0;
    if (!entry || !client || !size || size >= sizeof(block.text)) return false;
    block.length = static_cast<uint32_t>(size);
    std::memcpy(block.text,text,size);
    const TextBlock* nativeString = &block;
    const TextBlock* emptyString = nullptr;
    __try {
        reinterpret_cast<void(__stdcall*)(void*,const void*,const void*)>(entry)(reinterpret_cast<void*>(client),&emptyString,&nativeString);
    } __except(EXCEPTION_EXECUTE_HANDLER) { fault = true; return false; }
    return true;
}
