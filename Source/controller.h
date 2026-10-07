#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>

namespace Controller {
enum Button : uint16_t {
    Up=0x0001, Down=0x0002, Left=0x0004, Right=0x0008,
    Start=0x0010, Back=0x0020, LS=0x0040, RS=0x0080,
    LB=0x0100, RB=0x0200, A=0x1000, B=0x2000, X=0x4000, Y=0x8000
};
struct Settings {
    bool enabled=true, hints=true, invertY=false;
    unsigned deadzone=20, speed=100, trigger=20;
    std::array<unsigned,8> slots{{1,2,3,4,5,6,7,8}};
    bool Valid() const {
        return deadzone>=10 && deadzone<=40 && speed>=25 && speed<=200 && trigger>=10 && trigger<=80 &&
            std::all_of(slots.begin(),slots.end(),[](unsigned slot) { return slot>=1 && slot<=8; });
    }
    bool Load(std::istream& input) {
        Settings value;
        unsigned version=0,on=0,show=0,invert=0;
        if (!(input>>version>>on>>show>>value.deadzone>>value.speed>>value.trigger>>invert) ||
            version!=1 || on>1 || show>1 || invert>1) return false;
        for (auto& slot:value.slots) if (!(input>>slot)) return false;
        char extra=0;
        if (input>>extra || !value.Valid()) return false;
        value.enabled=on!=0; value.hints=show!=0; value.invertY=invert!=0;
        *this=value;
        return true;
    }
    bool Write(std::ostream& output) const {
        if (!Valid()) return false;
        output<<1<<' '<<enabled<<' '<<hints<<' '<<deadzone<<' '<<speed<<' '<<trigger<<' '<<invertY;
        for (auto slot:slots) output<<' '<<slot;
        output<<'\n';
        return output.good();
    }
};
struct Pad {
    bool connected=false;
    uint16_t buttons=0;
    int16_t lx=0,ly=0,rx=0,ry=0;
    uint8_t lt=0,rt=0;
};
struct Stick { float x=0,y=0; };
inline Stick Normalize(int16_t x,int16_t y,unsigned deadzone) {
    const float fx=x/32768.f,fy=y/32768.f,length=std::sqrt(fx*fx+fy*fy),zone=deadzone/100.f;
    if (length<=zone) return {};
    const float scale=(std::min(length,1.f)-zone)/((1.f-zone)*length);
    return {fx*scale,fy*scale};
}
constexpr uint64_t Action(unsigned id) { return uint64_t(1)<<id; }
struct Frame {
    uint64_t actions=0;
    unsigned mouse=0,items=0;
    int wheel=0;
    float dx=0,dy=0;
    bool connected=false,pointer=false,layer=false,waiting=false;
};
class Input {
    uint16_t previous=0;
    uint64_t lastTime=0,nextScroll=0;
    bool armed=false,manualPointer=false,lastPointer=false,haveTime=false,leftTrigger=false,rightTrigger=false;
    unsigned direction=0;
    static bool Trigger(uint8_t value,unsigned threshold,bool held) {
        const unsigned percent=held ? threshold-5 : threshold;
        return value>255*percent/100;
    }
public:
    void Reset() { *this={}; }
    Frame Step(const Pad& pad,const Settings& settings,bool focused,bool gameplay,uint64_t now) {
        Frame out;
        out.connected=pad.connected;
        if (!settings.enabled || !settings.Valid() || !pad.connected || !focused) { Reset(); return out; }
        const auto left=Normalize(pad.lx,pad.ly,settings.deadzone),right=Normalize(pad.rx,pad.ry,settings.deadzone);
        const bool neutral=!pad.buttons && !left.x && !left.y && !right.x && !right.y &&
            !Trigger(pad.lt,settings.trigger,false) && !Trigger(pad.rt,settings.trigger,false);
        const uint64_t elapsed=haveTime && now>=lastTime ? now-lastTime : 0;
        if (haveTime && (now<lastTime || elapsed>250)) armed=false;
        lastTime=now; haveTime=true;
        uint16_t pressed=pad.buttons & ~previous;
        if (!armed) {
            previous=pad.buttons;
            armed=neutral;
            leftTrigger=rightTrigger=false; direction=0;
            out.waiting=!armed; out.pointer=!gameplay || manualPointer;
            lastPointer=out.pointer;
            return out;
        }
        if (pressed&RS) manualPointer=!manualPointer;
        out.pointer=!gameplay || manualPointer;
        out.layer=(pad.buttons&LB)!=0;
        if (out.pointer!=lastPointer) {
            lastPointer=out.pointer; armed=neutral; previous=pad.buttons;
            leftTrigger=rightTrigger=false; direction=0;
            out.waiting=!armed;
            return out;
        }
        previous=pad.buttons;
        const float seconds=std::min(elapsed,uint64_t(50))/1000.f;
        const float speed=settings.speed/100.f*(out.pointer && out.layer ? .3f : 1.f);
        out.dx=right.x*std::fabs(right.x)*speed*seconds;
        out.dy=-right.y*std::fabs(right.y)*speed*seconds*(settings.invertY ? -1 : 1);
        if (out.pointer) {
            out.dx+=left.x*std::fabs(left.x)*speed*seconds;
            out.dy-=left.y*std::fabs(left.y)*speed*seconds;
            if (pad.buttons&A) out.mouse|=1;
            if (pad.buttons&X) out.mouse|=2;
            if (pressed&B) out.actions|=Action(1);
            if (pressed&Y) out.actions|=Action(2);
            if (pressed&LS) out.actions|=Action(3);
            if (!(pad.buttons&(Up|Down))) nextScroll=0;
            else if (now>=nextScroll) {
                out.wheel=(pad.buttons&Up ? 1 : 0)-(pad.buttons&Down ? 1 : 0);
                nextScroll=now+160;
            }
        } else {
            const float length=std::sqrt(left.x*left.x+left.y*left.y);
            const auto axis=[&](float amount,unsigned bit) { return length>0 && amount>length*(direction&bit ? .30f : .42f); };
            direction=(axis(left.y,1) ? 1u : 0u) | (axis(-left.y,2) ? 2u : 0u) |
                (axis(-left.x,4) ? 4u : 0u) | (axis(left.x,8) ? 8u : 0u);
            for (unsigned i=0;i<4;++i) if (direction&(1u<<i)) out.actions|=Action(10+i);
            constexpr uint16_t face[]={A,B,X,Y};
            for (unsigned i=0;i<4;++i) if (pressed&face[i]) out.actions|=Action(17+settings.slots[i+(out.layer ? 4 : 0)]);
            if (out.layer) {
                if (pad.buttons&Left) out.actions|=Action(29);
                if (pad.buttons&Right) out.actions|=Action(30);
                if (pad.buttons&Up) out.actions|=Action(27);
                if (pad.buttons&Down) out.actions|=Action(28);
            } else {
                if (pressed&Left) out.items|=1;
                if (pressed&Right) out.items|=2;
                if (pressed&Down) out.items|=4;
                if (pressed&Up) out.actions|=Action(9);
            }
            if (pad.buttons&RB) out.actions|=Action(51);
            if (pressed&LS) out.actions|=Action(31);
        }
        leftTrigger=Trigger(pad.lt,settings.trigger,leftTrigger);
        rightTrigger=Trigger(pad.rt,settings.trigger,rightTrigger);
        if (rightTrigger) out.mouse|=1;
        if (leftTrigger) out.mouse|=2;
        if (pressed&Start) out.actions|=Action(1);
        if (pressed&Back) out.actions|=Action(5);
        return out;
    }
};
struct Status {
    bool connected=false,pointer=false,layer=false,waiting=false;
    unsigned slot=0;
    const char* message="Connect a gamepad to begin.";
};
}
