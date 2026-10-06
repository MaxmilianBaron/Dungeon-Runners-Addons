#pragma once
#include "windows_compat.h"
#include <windows.h>
#include <Xinput.h>
#include "controller.h"
#include "native_reader.h"

class NativeController {
    using GetState=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
    GetState getState=nullptr;
    bool attempted=false,profileChecked=false,itemProfile=false;
    DWORD slot=0;
    uint64_t nextProbe=0,messageUntil=0;
    std::array<bool,256> held{};
    unsigned mouse=0;
    float remainderX=0,remainderY=0;
    Controller::Input input;
    Controller::Status status;
    static constexpr ULONG_PTR InputMarker=0x41524350;

    static bool Key(unsigned key,bool down) {
        INPUT event{};
        event.type=INPUT_KEYBOARD;
        event.ki.wVk=static_cast<WORD>(key);
        event.ki.wScan=static_cast<WORD>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC));
        event.ki.dwFlags=down ? 0 : KEYEVENTF_KEYUP;
        if ((key>=VK_PRIOR && key<=VK_DOWN) || key==VK_INSERT || key==VK_DELETE || key==VK_DIVIDE || key==VK_RCONTROL || key==VK_RMENU)
            event.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
        event.ki.dwExtraInfo=InputMarker;
        return SendInput(1,&event,sizeof(event))==1;
    }
    static bool Mouse(unsigned button,bool down) {
        INPUT event{};
        event.type=INPUT_MOUSE;
        event.mi.dwFlags=button==1 ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP) : (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
        event.mi.dwExtraInfo=InputMarker;
        return SendInput(1,&event,sizeof(event))==1;
    }
    static bool Foreground(HWND window) {
        DWORD process=0;
        return window && IsWindow(window) && !IsIconic(window) && GetForegroundWindow()==window &&
            GetWindowThreadProcessId(window,&process)==GetCurrentThreadId() && process==GetCurrentProcessId();
    }
    static bool CursorInClient(HWND window) {
        POINT point{}; RECT area{};
        if (!GetCursorPos(&point) || GetAncestor(WindowFromPoint(point),GA_ROOT)!=window ||
            !ScreenToClient(window,&point) || !GetClientRect(window,&area)) return false;
        return PtInRect(&area,point)!=FALSE;
    }
    void Message(const char* text,uint64_t now) { status.message=text; messageUntil=now+5000; }
    Controller::Pad ReadPad(uint64_t now) {
        if (!attempted) {
            attempted=true;
            for (auto name:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"}) {
                const auto module=WindowsCompat::SystemLibrary(name);
                if (!module) continue;
                getState=reinterpret_cast<GetState>(GetProcAddress(module,"XInputGetState"));
                if (getState) break;
                FreeLibrary(module);
            }
        }
        Controller::Pad pad;
        if (!getState || now<nextProbe) return pad;
        XINPUT_STATE state{};
        if (getState(slot,&state)!=ERROR_SUCCESS) {
            for (slot=0;slot<XUSER_MAX_COUNT;++slot) if (getState(slot,&state)==ERROR_SUCCESS) break;
            if (slot==XUSER_MAX_COUNT) { slot=0; nextProbe=now+500; return pad; }
            Release();
        }
        const auto& value=state.Gamepad;
        pad={true,value.wButtons,value.sThumbLX,value.sThumbLY,value.sThumbRX,value.sThumbRY,value.bLeftTrigger,value.bRightTrigger};
        return pad;
    }
    static bool Visible(const NativeReader& reader,uintptr_t node) {
        uint32_t flags=0;
        return node && reader.Read(node+0xb4,flags) && (flags&8);
    }
    static uintptr_t KeyMap(const NativeReader& reader,uintptr_t image) {
        const auto manager=reader.Pointer(image+0x533d70),table=reader.Pointer(manager);
        if (!manager || reader.Pointer(table+0x2c)!=image+0x2ed1f0) return 0;
        const auto keyboard=reader.Pointer(manager+0x18),map=reader.Pointer(keyboard+0x5c);
        int32_t remapping=0;
        return keyboard && map && reader.Pointer(map)==image+0x4b9e24 && reader.Read(keyboard+0x50,remapping) && !remapping ? map : 0;
    }
    static bool Gameplay(const NativeReader& reader,uintptr_t image,bool overlay) {
        if (overlay || !reader.InWorld() || !reader.LocalAvatarAlive()) return false;
        const auto ui=reader.Pointer(image+0x5314b0),root=reader.Pointer(image+0x533e20),focus=reader.Pointer(root+0x1b4);
        if (!focus || reader.Pointer(ui+0x180)) return false;
        const auto current=reader.Pointer(focus+0x10),normal=reader.Pointer(focus+0x14);
        if (current && current!=normal) return false;
        for (unsigned offset:{0x1d8u,0x1dcu,0x1e0u,0x228u,0x230u,0x23cu,0x254u,0x260u,0x270u})
            if (Visible(reader,reader.Pointer(ui+offset))) return false;
        uint8_t chatting=0,editing=0;
        const auto chat=reader.Pointer(ui+0x21c),prompt=reader.Pointer(ui+0x268);
        if (chat && (!reader.Read(chat+0x20c,chatting) || chatting)) return false;
        if (prompt && (!reader.Read(prompt+0x179,editing) || editing)) return false;
        return KeyMap(reader,image)!=0;
    }
    static bool ItemCode(const NativeReader& reader,uintptr_t image) {
        if (image!=0x400000) return false;
        struct Region { uintptr_t rva; unsigned size; uint32_t hash; };
        constexpr Region regions[]={{0x380e0,364,0x2306746f},{0x37d60,849,0x4bb91080},{0x37cb0,167,0x844e57ec},
            {0x39130,136,0xc9a0e4a1},{0x38880,1669,0x2620d68d},{0x38f10,261,0xf272a873},{0x39020,262,0xca29ed56}};
        for (const auto& region:regions) {
            uint32_t hash=2166136261u;
            for (unsigned i=0;i<region.size;++i) {
                uint8_t byte=0;
                if (!reader.Read(image+region.rva+i,byte)) return false;
                hash=(hash^byte)*16777619u;
            }
            if (hash!=region.hash) return false;
        }
        return true;
    }
    static uintptr_t FindItem(const NativeReader& reader,uintptr_t image,uintptr_t node,uintptr_t label,unsigned depth,unsigned& budget) {
        if (!node || depth>24 || !budget) return 0;
        --budget;
        if (reader.Pointer(node)==image+0x44aeb0 && reader.Pointer(node+0x174)==label) return node;
        auto child=reader.Pointer(node+0x18);
        while (child && budget) {
            if (reader.Pointer(child+0x14)!=node) return 0;
            if (const auto match=FindItem(reader,image,child,label,depth+1,budget)) return match;
            const auto next=reader.Pointer(child+0x20);
            if (next==child) return 0;
            child=next;
        }
        return 0;
    }
    static bool InvokeItem(uintptr_t function,uintptr_t icon) {
        __try { reinterpret_cast<void (__thiscall*)(void*)>(function)(reinterpret_cast<void*>(icon)); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool UseItem(const NativeReader& reader,uintptr_t image,unsigned index) {
        if (!profileChecked) { itemProfile=ItemCode(reader,image); profileChecked=true; }
        if (!itemProfile) return false;
        const auto ui=reader.Pointer(image+0x5314b0),list=reader.Pointer(ui+0x234);
        if (reader.Pointer(list)!=image+0x44b150) return false;
        const auto label=reader.Pointer(list+0x1ac+index*4);
        if (!label) return false;
        unsigned budget=256;
        const auto icon=FindItem(reader,image,list,label,0,budget);
        if (!icon || !reader.Pointer(icon+0x16c)) return false;
        const auto begin=reader.Pointer(icon+0x17c),end=reader.Pointer(icon+0x180);
        if (!begin || end<=begin || end-begin>256 || (end-begin)%4) return false;
        return InvokeItem(image+0x380e0,icon);
    }
    void Apply(const Controller::Frame& frame,const NativeReader& reader,uintptr_t image,HWND window,uint64_t now) {
        std::array<bool,256> wanted{};
        std::array<std::array<uint32_t,4>,256> bindings{};
        const auto map=frame.actions ? KeyMap(reader,image) : 0;
        const bool mapped=map && reader.Read(map+0x1d0,bindings);
        for (unsigned action=0;action<64;++action) {
            if (!(frame.actions&Controller::Action(action))) continue;
            unsigned key=action==1 ? VK_ESCAPE : action==2 ? VK_RETURN : action==3 ? VK_TAB : action==51 ? VK_MENU : 0;
            if (!key && mapped) for (unsigned i=8;i<255;++i) if (bindings[i][0]==action) { key=i; break; }
            if (key) wanted[key]=true;
            else Message("Assign an unmodified game key to this action.",now);
        }
        bool success=true;
        for (unsigned key=0;key<256;++key) if (held[key] && !wanted[key]) {
            if (Key(key,false)) held[key]=false; else success=false;
        }
        for (unsigned bit:{1u,2u}) if ((mouse&bit) && !(frame.mouse&bit)) {
            if (Mouse(bit,false)) mouse&=~bit; else success=false;
        }
        if (!Foreground(window)) { Release(); return; }
        if ((GetAsyncKeyState(VK_CONTROL)&0x8000) || (GetAsyncKeyState(VK_SHIFT)&0x8000) || (GetAsyncKeyState(VK_MENU)&0x8000 && !held[VK_MENU])) {
            Release(); return;
        }
        for (unsigned key=0;key<256;++key) if (wanted[key] && !held[key] && !(GetAsyncKeyState(static_cast<int>(key))&0x8000)) {
            if (Key(key,true)) held[key]=true; else success=false;
        }
        if (frame.dx || frame.dy) {
            RECT area{}; POINT point{};
            if (GetClientRect(window,&area) && GetCursorPos(&point) && ScreenToClient(window,&point) && area.right>0 && area.bottom>0) {
                remainderX+=frame.dx*area.right; remainderY+=frame.dy*area.right;
                const int dx=static_cast<int>(remainderX),dy=static_cast<int>(remainderY);
                remainderX-=dx; remainderY-=dy;
                if (dx || dy) {
                    point.x=std::clamp(point.x+dx,0L,area.right-1); point.y=std::clamp(point.y+dy,0L,area.bottom-1);
                    if (ClientToScreen(window,&point) && Foreground(window)) SetCursorPos(point.x,point.y);
                }
            }
        } else remainderX=remainderY=0;
        const bool pointerInside=Foreground(window) && CursorInClient(window);
        for (unsigned bit:{1u,2u}) if (pointerInside && (frame.mouse&bit) && !(mouse&bit) && !(GetAsyncKeyState(bit==1 ? VK_LBUTTON : VK_RBUTTON)&0x8000)) {
            if (Mouse(bit,true)) mouse|=bit; else success=false;
        }
        if (pointerInside && frame.wheel) {
            INPUT event{}; event.type=INPUT_MOUSE; event.mi.dwFlags=MOUSEEVENTF_WHEEL;
            event.mi.mouseData=static_cast<DWORD>(frame.wheel*WHEEL_DELTA); event.mi.dwExtraInfo=InputMarker;
            if (SendInput(1,&event,sizeof(event))!=1) success=false;
        }
        for (unsigned i=0;i<3;++i) if (frame.items&(1u<<i)) {
            if (!Foreground(window) || !Gameplay(reader,image,false) || !UseItem(reader,image,i)) Message("Consumable action is unavailable.",now);
        }
        if (!success) { Release(); Message("Controller input could not be delivered.",now); }
    }
public:
    explicit NativeController(DWORD (WINAPI* provider)(DWORD,XINPUT_STATE*)=nullptr) : getState(provider),attempted(provider!=nullptr) {}
    const Controller::Status& Status() const { return status; }
    void Release() {
        for (unsigned key=0;key<held.size();++key) if (held[key] && Key(key,false)) held[key]=false;
        for (unsigned bit:{1u,2u}) if ((mouse&bit) && Mouse(bit,false)) mouse&=~bit;
        remainderX=remainderY=0; input.Reset();
    }
    void Service(const NativeReader& reader,uintptr_t image,const Controller::Settings& settings,HWND window,bool overlay,uint64_t now) {
        if (!settings.enabled) { Release(); status={}; status.message="Controller is off."; return; }
        const auto pad=ReadPad(now);
        if (!pad.connected) { Release(); status={}; status.message="Connect a gamepad. Steam Input: Gamepad template."; return; }
        const bool focused=Foreground(window);
        const auto frame=input.Step(pad,settings,focused,Gameplay(reader,image,overlay),now);
        status.connected=frame.connected; status.pointer=frame.pointer; status.layer=frame.layer; status.waiting=frame.waiting; status.slot=slot+1;
        if (!focused) status.message="Paused while the game is in the background.";
        else if (frame.waiting) status.message="Release the sticks and buttons to continue.";
        else if (now>=messageUntil) status.message=frame.pointer ? "Cursor mode. R3 returns to combat." : "Combat mode. Hold LB for skills 5-8.";
        if (!focused) { Release(); return; }
        Apply(frame,reader,image,window,now);
    }
};
