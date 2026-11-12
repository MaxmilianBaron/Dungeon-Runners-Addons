#pragma once
#include <windows.h>
#include "chat_timestamps.h"
#include "AardvarkHook/patch.h"

extern "C" bool __cdecl MeterOverlayChatTimestampsEnabled();

template<class Reader> bool ChatTimestampProfileMatches(const Reader& reader,uintptr_t image) {
    if (image!=0x400000) return false;
    struct Region { uintptr_t rva; unsigned size; uint32_t hash; };
    constexpr Region regions[] = {
        {0x201c0,2766,0x5568e2ed},
        {0x2a9ad0,141,0xc652e8a7},
        {0x1610,155,0x50ed6da6}
    };
    for (const auto& region:regions) {
        uint32_t hash=2166136261u;
        for (unsigned i=0;i<region.size;++i) {
            uint8_t value=0;
            if (!reader.Read(image+region.rva+i,value)) return false;
            hash=(hash^value)*16777619u;
        }
        if (hash!=region.hash) return false;
    }
    return true;
}

namespace ChatTimestampNative {
inline uintptr_t continuation=0,setter=0;
inline void* original=nullptr;
using SetTextEntry=void(__thiscall*)(void*,const void*);

static bool Copy(const void* text,ChatTimestampText& output,unsigned hour,unsigned minute) {
    __try {
        const auto* block=text ? *static_cast<const ChatTimestampText* const*>(text) : nullptr;
        return block && block->length < ChatTimestampText::Capacity-ChatTimestampText::PrefixLength &&
            !block->text[block->length] && output.Assign(hour,minute,block->text,block->length);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static __declspec(noinline) void Stamped(void* label,uintptr_t entry,const void* text) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    ChatTimestampText output;
    const ChatTimestampText* block=&output;
    reinterpret_cast<SetTextEntry>(entry)(label,Copy(text,output,now.wHour,now.wMinute) ? &block : text);
}

static void __stdcall SetText(void* label,uintptr_t entry,const void* text) {
    if (entry==setter && MeterOverlayChatTimestampsEnabled()) Stamped(label,entry,text);
    else reinterpret_cast<SetTextEntry>(entry)(label,text);
}

static __declspec(naked) void Hook() {
    __asm pushfd
    __asm pushad
    __asm lea eax,[esp+0x34]
    __asm push eax
    __asm push edx
    __asm push ebx
    __asm call SetText
    __asm popad
    __asm popfd
    __asm jmp dword ptr [continuation]
}

static bool Install(uintptr_t image,bool compatible) {
    if (!compatible) return false;
    static NativePatchSet patches;
    continuation=image+0x206e1;
    setter=image+0x2a9ad0;
    const NativePatchSpec spec{reinterpret_cast<void*>(image+0x206d8),reinterpret_cast<void*>(Hook),
        &original,"8d442410508bcbffd28b45088b4c24288b8c884402",9};
    return patches.Install(&spec,1);
}
}
