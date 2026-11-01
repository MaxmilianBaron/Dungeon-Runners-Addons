#pragma once
#include <cstdint>

template<class Reader> bool EnhancedZoomProfileMatches(const Reader& reader,uintptr_t image) {
    if (image!=0x400000) return false;
    struct Region { uintptr_t rva; unsigned size; uint32_t hash; };
    constexpr Region regions[] = {
        {0x27c10,346,0x97138145},
        {0x28a50,681,0x981d5029},
        {0x72400,29,0xc8895f28},
        {0x28d00,153,0xeef3f055},
        {0x76750,17,0x7dd1743d},
        {0x72030,656,0x3262ed7e},
        {0x72470,2286,0x67879aff},
        {0x72d60,617,0xf3a3902b},
        {0xd8280,1498,0xeb5a43b8},
        {0x2272c0,91,0x1ae4554b},
        {0xd7d40,134,0x6f49dada},
        {0x250300,134,0xea1784e5},
        {0x53700,89,0x7d8db2fc},
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
    uint32_t maximum=0,minimum=0;
    return reader.Read(image+0x4cec54,maximum) && maximum==0x432a0000 &&
        reader.Read(image+0x4cebd8,minimum) && minimum==0x42200000 &&
        reader.Pointer(image+0x468cd0+0x38)==image+0x2272c0;
}
