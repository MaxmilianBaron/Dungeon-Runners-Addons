#pragma once
#include "AardvarkUI/layout.h"

namespace TradeAuction {
struct WindowLayout {
    static constexpr float Width=526,Height=562;
    static constexpr float DefaultScale=1.12f,MinScale=.72f,MaxScale=2.25f;
    UiWindowAnchor anchor{{.5f,.5f},true};
    float preferredScale=DefaultScale;

    static float FitLimit(UiPoint display) {
        return std::max(0.0f,std::min({MaxScale,(display.x-16)/Width,(display.y-16)/Height}));
    }
    float Scale(UiPoint display) const { return std::min(preferredScale,FitLimit(display)); }
    UiPoint Size(UiPoint display) const {
        const float scale=Scale(display);
        return {Width*scale,Height*scale};
    }
    UiPoint Origin(UiPoint display) const { return anchor.Resolve(display,Size(display)); }
    void MoveTo(UiPoint position,UiPoint display) { anchor.Capture(position,display,Size(display)); }
    void ResizeFrom(UiPoint origin,UiPoint delta,float startScale,UiPoint display) {
        const float maximum=std::max(0.0f,std::min({FitLimit(display),(display.x-origin.x)/Width,(display.y-origin.y)/Height}));
        const float change=(delta.x*Width+delta.y*Height)/(Width*Width+Height*Height);
        if (maximum>0) preferredScale=std::max(MinScale,std::clamp(startScale+change,std::min(MinScale,maximum),maximum));
        MoveTo(origin,display);
    }
};
}
