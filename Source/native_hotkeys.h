#pragma once
#include "native_reader.h"

class NativeHotkeys {
    const NativeReader& reader;
    uintptr_t image;
public:
    NativeHotkeys(const NativeReader& source,uintptr_t base) : reader(source),image(base) {}
    AddonHotkeyState Check(unsigned key,unsigned modifiers,bool activation) const {
        if (!key && !modifiers) return AddonHotkeyState::Available;
        if (!key || key>255 || modifiers>7 || !reader.InWorld()) return AddonHotkeyState::Unavailable;
        const auto manager=reader.Pointer(image+0x533d70), table=reader.Pointer(manager);
        if (!manager || !table || reader.Pointer(table+0x2c)!=image+0x2ed1f0) return AddonHotkeyState::Unavailable;
        const auto keyboard=reader.Pointer(manager+0x18), map=reader.Pointer(keyboard+0x5c);
        int32_t remapping=0;
        if (!keyboard || !map || !reader.Read(keyboard+0x50,remapping)) return AddonHotkeyState::Unavailable;
        if (remapping) return AddonHotkeyState::Busy;
        const unsigned nativeModifiers=(modifiers&1 ? 2u : 0u) | (modifiers&4 ? 1u : 0u);
        uint32_t action=0;
        if (!reader.Read(map+0x1d0+key*16+nativeModifiers*4,action)) return AddonHotkeyState::Unavailable;
        if (action) return AddonHotkeyState::Conflict;
        for (const auto modifier:std::array<std::array<unsigned,3>,3>{{{1,0x11,2},{2,0x12,0},{4,0x10,1}}}) {
            if (!(modifiers&modifier[0])) continue;
            for (unsigned prefix=0;prefix<4;++prefix) {
                if ((prefix&nativeModifiers)!=prefix || (prefix&modifier[2])!=modifier[2]) continue;
                if (!reader.Read(map+0x1d0+modifier[1]*16+prefix*4,action)) return AddonHotkeyState::Unavailable;
                if (action) return AddonHotkeyState::Conflict;
            }
        }
        if (!activation) return AddonHotkeyState::Available;
        const auto ui=reader.Pointer(image+0x5314b0), root=reader.Pointer(image+0x533e20);
        const auto focus=reader.Pointer(root+0x1b4);
        uint32_t focused=0,normal=0,modal=0;
        if (!ui || !root || !focus || !reader.Read(focus+0x10,focused) || !reader.Read(focus+0x14,normal) || !reader.Read(ui+0x180,modal)) return AddonHotkeyState::Unavailable;
        if (modal || (focused && focused!=normal)) return AddonHotkeyState::Busy;
        uint32_t chat=0,prompt=0;
        uint8_t chatting=0,editing=0;
        if (!reader.Read(ui+0x21c,chat) || !reader.Read(ui+0x268,prompt)) return AddonHotkeyState::Unavailable;
        if (chat && !reader.Read(chat+0x20c,chatting)) return AddonHotkeyState::Unavailable;
        if (prompt && !reader.Read(prompt+0x179,editing)) return AddonHotkeyState::Unavailable;
        return chatting || editing ? AddonHotkeyState::Busy : AddonHotkeyState::Available;
    }
};
