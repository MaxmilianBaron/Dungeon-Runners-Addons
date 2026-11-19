#pragma once
#include "native_reader.h"
#include "sally_sam.h"

namespace SallySam {
struct Definition { const char* key; Kind kind; uint16_t offers; };
#include "sally_sam_catalog.generated.h"

class Tracker {
    Frame frame;
    uint64_t nextScan=0;

    static Map ReadMap(const NativeReader& reader,uintptr_t image) {
        Map map;
        if (!reader.InWorld(&map.character)) return {};
        const auto ui=reader.Pointer(image+0x5314b0);
        const auto control=reader.Pointer(ui+0x290);
        if (!control || reader.Pointer(control+0x170)!=ui) return {};
        map.zone=reader.Pointer(ui+0x1b4);
        const auto player=reader.Pointer(map.zone+0xf8);
        const auto avatar=reader.Pointer(player+0xb0);
        map.manager=reader.Pointer(player+0x88);
        if (!map.manager || !avatar || reader.Pointer(avatar)!=image+0x46de00 ||
            reader.Pointer(avatar+0x14)!=player || reader.Pointer(avatar+0x88)!=map.manager ||
            reader.Pointer(reader.Pointer(map.manager)+0xa4)!=image+0x1d9b30 ||
            reader.Pointer(reader.Pointer(map.manager+0x14)+0xf4)!=map.manager) return {};
        const auto visual=reader.Pointer(avatar+0xb0),mini=control+0x174,root=reader.Pointer(mini+0x10);
        int32_t x=0,y=0,width=0,height=0;
        uint8_t visible=0;
        std::array<int32_t,4> rect{};
        std::array<float,5> bounds{};
        if (!visual || reader.Pointer(visual+0xcc)!=avatar || !reader.Read(visual+0x68,x) || !reader.Read(visual+0x6c,y) ||
            !reader.Read(mini+0x14,map.mode) || (map.mode!=2 && map.mode!=3) ||
            !reader.Read(mini+0x140,visible) || !visible || !reader.Read(mini+0xc,map.zoom) ||
            !map.zoom || map.zoom>1000 || !reader.Read(control+0x290,rect) || !reader.Read(mini+0x144,bounds) ||
            !root || !reader.Read(root+0xf8,width) || !reader.Read(root+0xfc,height) ||
            width<320 || width>32768 || height<200 || height>32768 || !reader.Read(map.zone+0xc8,map.seed)) return {};
        map.rect={float(rect[0]),float(rect[1]),float(rect[2]),float(rect[3])};
        map.bounds={bounds[0],bounds[1],bounds[3],bounds[4]};
        map.player={x/256.f,y/256.f};
        map.screen={float(width),float(height)};
        return map.rect.Valid() && (map.mode==2 || map.bounds.Valid()) ? map : Map{};
    }

    static const Definition* Identify(const NativeReader& reader,uintptr_t node) {
        for (unsigned depth=0;node && depth<24;++depth,node=reader.Pointer(node+0x54)) {
            const auto definition=reader.Pointer(node+0x58);
            if (!definition) continue;
            auto key=reader.String(definition+0x18,256);
            for (char& character:key) {
                if (character=='\\' || character=='/') character='.';
                else if (character>='A' && character<='Z') character=static_cast<char>(character+('a'-'A'));
            }
            const auto* entry=std::lower_bound(std::begin(Definitions),std::end(Definitions),key,
                [](const Definition& value,const std::string& name){ return value.key<name; });
            if (entry!=std::end(Definitions) && key==entry->key) return entry;
        }
        return nullptr;
    }

    static std::vector<Vendor> Scan(const NativeReader& reader,uintptr_t image,const Map& map) {
        const auto first=reader.Pointer(map.manager+0xa3c),last=reader.Pointer(map.manager+0xa40);
        if (!first || last<first || last-first>65536 || (last-first)%4) return {};
        std::vector<Vendor> found;
        for (auto at=first;at<last && found.size()<64;at+=4) {
            const auto node=reader.Pointer(at);
            if (!node || reader.Pointer(node+0x88)!=map.manager || !reader.KindOf(node,image+0x5308dc)) continue;
            if (const auto* definition=Identify(reader,node)) {
                int32_t x=0,y=0;
                if (reader.Read(node+0x90,x) && reader.Read(node+0x94,y))
                    found.push_back({definition->kind,definition->offers,{x/256.f,y/256.f}});
            }
        }
        if (first!=reader.Pointer(map.manager+0xa3c) || last!=reader.Pointer(map.manager+0xa40) ||
            !map.SameArea(ReadMap(reader,image))) return {};
        return found;
    }

public:
    const Frame& Update(const NativeReader& reader,uintptr_t image,bool enabled,uint64_t now) {
        const auto map=enabled ? ReadMap(reader,image) : Map{};
        if (!map.zone) { frame={}; nextScan=0; }
        else {
            if (now>=nextScan || !map.SameArea(frame.map)) {
                frame.vendors=Scan(reader,image,map);
                nextScan=now+400;
            }
            frame.map=map;
        }
        return frame;
    }
};
}
