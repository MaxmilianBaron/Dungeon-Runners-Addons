#pragma once
#include "boss_chest_info.h"
#include <array>
#include <cstdint>

template<class Reader> BossChestPresence ReadBossChestPresence(const Reader& r,uintptr_t image) {
    if (!r.InWorld()) return BossChestPresence::Unknown;
    const auto ui=r.Pointer(image+0x5314b0),zone=r.Pointer(ui+0x1b4),world=r.Pointer(zone+0xf0),manager=r.Pointer(world+0xf4);
    uint32_t seed=0;
    std::array<uint32_t,3> span{};
    if (!zone || !world || !manager || r.Pointer(r.Pointer(manager)+0xa4)!=image+0x1d9b30 || !r.Read(zone+0xc8,seed) ||
        !r.Read(manager+0xa3c,span) || span[0]<0x10000 || span[1]<=span[0] || span[2]<span[1] ||
        span[2]-span[0]>65536*4 || (span[1]-span[0])%4 || (span[2]-span[0])%4) return BossChestPresence::Unknown;
    bool found=false;
    for (uintptr_t at=span[0];at<span[1];at+=4) {
        uint32_t entity=0;
        if (!r.Read(at,entity) || entity<0x10000) return BossChestPresence::Unknown;
        const auto type=r.Pointer(entity);
        if (!type) return BossChestPresence::Unknown;
        if (type!=image+0x4984b8) continue;
        if (r.Pointer(type+0x24)!=image+0x4240 || r.Pointer(type+8)!=image+0x1a8c00) return BossChestPresence::Unknown;
        const auto block=r.Pointer(entity+0x10);
        uint32_t length=0;
        if (block && (!r.Read(block,length) || length>128)) return BossChestPresence::Unknown;
        const auto name=r.String(entity+0x10,128);
        if (length && name.empty()) return BossChestPresence::Unknown;
        if (name=="DungeonBossChest") { found=true; break; }
    }
    std::array<uint32_t,3> current{};
    uint32_t currentSeed=0;
    if (!r.InWorld() || r.Pointer(ui+0x1b4)!=zone || r.Pointer(zone+0xf0)!=world || r.Pointer(world+0xf4)!=manager ||
        !r.Read(zone+0xc8,currentSeed) || currentSeed!=seed || !r.Read(manager+0xa3c,current) || current!=span) return BossChestPresence::Unknown;
    return found ? BossChestPresence::Found : BossChestPresence::NotDetected;
}
