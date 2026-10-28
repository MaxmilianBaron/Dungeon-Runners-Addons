#pragma once
#include <cstdint>
#include <cstring>

inline constexpr size_t NativeLocalTextCapacity = 4096;

inline bool CopyNativeItemLink(uintptr_t entry,uintptr_t release,uintptr_t item,char (&text)[NativeLocalTextCapacity],bool& fault) {
    struct TextBlock { uint32_t length; char text[1]; };
    TextBlock* block = nullptr;
    bool copied = false;
    text[0] = 0;
    if (!entry || !release || !item || fault) return false;
    __try {
        reinterpret_cast<void*(__stdcall*)(TextBlock**,void*)>(entry)(&block,reinterpret_cast<void*>(item));
        if (block && block->length && block->length < sizeof(text) && !block->text[block->length] &&
            !std::memchr(block->text,0,block->length)) {
            std::memcpy(text,block->text,block->length+1);
            copied = true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { fault = true; }
    if (block) {
        __try { reinterpret_cast<void(__cdecl*)(void*)>(release)(block); }
        __except(EXCEPTION_EXECUTE_HANDLER) { fault = true; }
    }
    if (fault || !copied) text[0] = 0;
    return copied && !fault;
}

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
    struct TextBlock { uint32_t length; char text[NativeLocalTextCapacity]; } block{};
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
