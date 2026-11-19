#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace SallySam {
enum class Kind : unsigned { Sally, NormalSally, KcSally, MythicSally, UniqueSam, MythicSam };
inline constexpr const char* Names[]={"Sally","Normal Sally","KC Sally","Mythic Sally","Unique Sam","Mythic Sam"};
struct Point { float x=0,y=0; };
struct Rect {
    float left=0,top=0,right=0,bottom=0;
    float Width() const { return right-left; }
    float Height() const { return bottom-top; }
    bool Valid() const { return Width()>8 && Width()<=32768 && Height()>8 && Height()<=32768; }
};
struct Vendor {
    Kind kind=Kind::Sally;
    uint16_t offers=0;
    Point position;
};
struct Map {
    uintptr_t zone=0,manager=0;
    uint32_t character=0,seed=0,mode=0,zoom=0;
    Rect rect,bounds;
    Point player,screen;
    bool SameArea(const Map& other) const {
        return zone==other.zone && manager==other.manager && character==other.character && seed==other.seed;
    }
};
struct Frame { Map map; std::vector<Vendor> vendors; };

inline Point Project(const Map& map,Point position,float inset,bool& edge) {
    const float w=map.rect.Width(),h=map.rect.Height();
    Point center=map.player;
    float sx=std::min(w,h)/(70.f*static_cast<float>(map.zoom)),sy=sx;
    if (map.mode==3) {
        const float span=std::max(map.bounds.Width(),map.bounds.Height());
        const float size=std::max(2000.f,span);
        center={map.bounds.right-span*.5f,map.bounds.bottom-span*.5f};
        sx=w*map.screen.y/(size*map.screen.x);
        sy=h/size;
    }
    Point offset{(position.x-center.x)*sx,(center.y-position.y)*sy};
    const float xLimit=std::max(1.f,w*.5f-inset),yLimit=std::max(1.f,h*.5f-inset);
    const float limit=map.mode==2 ? std::hypot(offset.x,offset.y)/std::min(xLimit,yLimit) :
        std::max(std::abs(offset.x)/xLimit,std::abs(offset.y)/yLimit);
    edge=limit>1;
    if (edge) { offset.x/=limit; offset.y/=limit; }
    return {map.rect.left+w*.5f+offset.x,map.rect.top+h*.5f+offset.y};
}

inline std::string Label(const Vendor& vendor) {
    std::string text=Names[static_cast<unsigned>(vendor.kind)];
    static constexpr const char* categories[]={"1H","2H","Shield","Body","Shoulders","Helm","Gloves","Boots","Jewelry"};
    bool first=true;
    for (unsigned i=0;i<9;++i) if (vendor.offers&(1u<<i)) {
        text+=first ? "\n" : " / ";
        text+=categories[i];
        first=false;
    }
    return text;
}
}
