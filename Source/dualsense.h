#pragma once
#include "controller.h"

namespace Controller {
struct DualSenseState {
    std::array<int32_t,6> axes{{0,0,0,-32768,-32768,0}};
    uint32_t buttons=0,pov=0xffffffffu;
};
inline bool DualSenseProduct(uint16_t vendor,uint16_t product) { return vendor==0x054c && (product==0x0ce6 || product==0x0df2); }
inline Pad DualSensePad(const DualSenseState& state) {
    for (auto axis:state.axes) if (axis<-32768 || axis>32767) return {};
    Pad pad;
    pad.connected=pad.playstation=true;
    const auto invert=[](int32_t axis) { return static_cast<int16_t>(std::clamp(-axis,-32768,32767)); };
    const auto trigger=[](int32_t axis) { return static_cast<uint8_t>((uint32_t(axis+32768)*255u+32767u)/65535u); };
    pad.lx=static_cast<int16_t>(state.axes[0]); pad.ly=invert(state.axes[1]);
    pad.rx=static_cast<int16_t>(state.axes[2]); pad.ry=invert(state.axes[5]);
    pad.lt=trigger(state.axes[3]); pad.rt=trigger(state.axes[4]);
    constexpr uint16_t mapping[]={X,A,B,Y,LB,RB,0,0,Back,Start,LS,RS};
    for (unsigned bit=0;bit<12;++bit) if (state.buttons&(1u<<bit)) pad.buttons|=mapping[bit];
    if (state.pov<36000) {
        constexpr uint16_t hats[]={Up,Up|Right,Right,Down|Right,Down,Down|Left,Left,Up|Left};
        pad.buttons|=hats[((state.pov+2250)/4500)%8];
    }
    return pad;
}
}
