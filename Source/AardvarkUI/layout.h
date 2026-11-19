#pragma once
#include "ui.h"
#include <algorithm>
#include <cmath>

struct UiWindowAnchor {
    UiPoint fraction;
    bool saved=false;

    static UiPoint Travel(UiPoint display,UiPoint size) {
        return {std::max(0.0f,display.x-size.x),std::max(0.0f,display.y-size.y)};
    }

    void Capture(UiPoint position,UiPoint display,UiPoint size) {
        const auto room=Travel(display,size);
        if (room.x>0) fraction.x=std::clamp(position.x/room.x,0.0f,1.0f);
        if (room.y>0) fraction.y=std::clamp(position.y/room.y,0.0f,1.0f);
        saved=true;
    }

    UiPoint Resolve(UiPoint display,UiPoint size) const {
        const auto room=Travel(display,size);
        return {fraction.x*room.x,fraction.y*room.y};
    }
};

inline UiPoint ProjectUiPoint(UiPoint point,UiPoint source,UiPoint target) {
    return {point.x*target.x/source.x,point.y*target.y/source.y};
}

struct UiLinePair {
    float first,second,size;
};

inline UiLinePair CenterTwoLines(float top,float bottom,float preferredSize,float preferredGap) {
    top=std::ceil(top);
    bottom=std::floor(bottom);
    const auto height=std::max(0.0f,bottom-top);
    const auto size=std::min(preferredSize,std::floor(height*.5f));
    const auto remaining=height-2*size;
    const auto gap=std::min(preferredGap,remaining);
    const auto first=std::round(top+(remaining-gap)*.5f);
    return {first,first+size+gap,size};
}

inline bool AttachedButton(UiPoint position,UiPoint extent,UiPoint gameScale,UiPoint display,UiPoint& origin,UiPoint& size) {
    const auto left=std::round(position.x*gameScale.x);
    const auto right=std::round((position.x+extent.x)*gameScale.x);
    const auto preferredHeight=std::round(18*gameScale.y);
    auto top=std::round((position.y+extent.y+3)*gameScale.y);
    auto height=std::min(preferredHeight,display.y-top);
    if (height<12*gameScale.y) {
        height=preferredHeight;
        top=std::round((position.y-3)*gameScale.y)-height;
    }
    if (left<0 || right>display.x || top<0 || top+height>display.y || right-left<48*gameScale.x || height<12*gameScale.y) return false;
    origin={left,top};
    size={right-left,height};
    return true;
}
