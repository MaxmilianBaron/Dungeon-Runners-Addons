#pragma once
#include <windows.h>
#include <array>
#include <string>

struct HotkeyBinding {
    unsigned key=VK_F8, modifiers=0;
    static bool Modifier(unsigned value) {
        return value==VK_CONTROL || value==VK_LCONTROL || value==VK_RCONTROL || value==VK_MENU || value==VK_LMENU || value==VK_RMENU || value==VK_SHIFT || value==VK_LSHIFT || value==VK_RSHIFT;
    }
    static unsigned Modifiers() {
        return (GetKeyState(VK_CONTROL)&0x8000 ? 1u : 0u) | (GetKeyState(VK_MENU)&0x8000 ? 2u : 0u) | (GetKeyState(VK_SHIFT)&0x8000 ? 4u : 0u) | ((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000 ? 8u : 0u);
    }
    bool Valid() const {
        if (modifiers>7 || key>255) return false;
        if (!key) return !modifiers;
        if (key==VK_F4 && (modifiers&2)) return false;
        if ((key>=VK_F1 && key<=VK_F24) || (key>=VK_PRIOR && key<=VK_DOWN) || key==VK_INSERT || key==VK_DELETE || key==VK_PAUSE) return true;
        const bool text=(key>='0' && key<='9') || (key>='A' && key<='Z') || (key>=VK_NUMPAD0 && key<=VK_DIVIDE) || (key>=VK_OEM_1 && key<=VK_OEM_3) || (key>=VK_OEM_4 && key<=VK_OEM_8) || key==VK_OEM_102 || key==VK_SPACE || key==VK_RETURN || key==VK_TAB;
        return text;
    }
    std::string Name() const {
        if (!key) return "None";
        std::string result;
        if (modifiers&1) result+="Ctrl+";
        if (modifiers&2) result+="Alt+";
        if (modifiers&4) result+="Shift+";
        if (key>=VK_F1 && key<=VK_F24) return result+"F"+std::to_string(key-VK_F1+1);
        if ((key>='0' && key<='9') || (key>='A' && key<='Z')) return result+static_cast<char>(key);
        if (key>=VK_NUMPAD0 && key<=VK_NUMPAD9) return result+"Num "+std::to_string(key-VK_NUMPAD0);
        switch (key) {
        case VK_PRIOR: return result+"Page Up";
        case VK_NEXT: return result+"Page Down";
        case VK_END: return result+"End";
        case VK_HOME: return result+"Home";
        case VK_LEFT: return result+"Left";
        case VK_UP: return result+"Up";
        case VK_RIGHT: return result+"Right";
        case VK_DOWN: return result+"Down";
        case VK_INSERT: return result+"Insert";
        case VK_DELETE: return result+"Delete";
        case VK_PAUSE: return result+"Pause";
        case VK_SPACE: return result+"Space";
        case VK_RETURN: return result+"Enter";
        case VK_TAB: return result+"Tab";
        }
        wchar_t name[64]{};
        const LONG scan=static_cast<LONG>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16);
        if (GetKeyNameTextW(scan,name,64)>0) {
            char text[256]{};
            if (WideCharToMultiByte(CP_UTF8,0,name,-1,text,sizeof(text),nullptr,nullptr)>0) return result+text;
        }
        return result+"Key "+std::to_string(key);
    }
};

enum class HotkeyEvent { Pass, Consume, Toggle, Changed, Cancelled, Invalid };

class HotkeyInput {
    std::array<unsigned,256> blocked{};
    static unsigned Scan(LPARAM value) { return static_cast<unsigned>((static_cast<uintptr_t>(value)>>16)&0x1ff)+1; }
public:
    bool recording=false;
    void Reset() { blocked={}; recording=false; }
    HotkeyEvent Message(UINT message,WPARAM key,LPARAM data,bool active,unsigned modifiers,const HotkeyBinding& binding,HotkeyBinding& draft) {
        if (message==WM_KILLFOCUS) { Reset(); return HotkeyEvent::Pass; }
        if ((message==WM_KEYUP || message==WM_SYSKEYUP) && key<blocked.size() && blocked[key]) { blocked[key]=0; return HotkeyEvent::Consume; }
        if (message==WM_CHAR || message==WM_SYSCHAR || message==WM_DEADCHAR || message==WM_SYSDEADCHAR) {
            for (const auto scan:blocked) if (scan==Scan(data)) return HotkeyEvent::Consume;
            return HotkeyEvent::Pass;
        }
        if (!active || (message!=WM_KEYDOWN && message!=WM_SYSKEYDOWN) || key>=blocked.size()) return HotkeyEvent::Pass;
        if (blocked[key]) return HotkeyEvent::Consume;
        const bool repeated=(static_cast<uintptr_t>(data)&(1ull<<30))!=0;
        if (repeated) return HotkeyEvent::Pass;
        if (recording) {
            blocked[key]=Scan(data);
            if (HotkeyBinding::Modifier(static_cast<unsigned>(key))) return HotkeyEvent::Consume;
            if (key==VK_ESCAPE) { recording=false; return HotkeyEvent::Cancelled; }
            if (key==VK_BACK) { draft={0,0}; recording=false; return HotkeyEvent::Changed; }
            HotkeyBinding candidate{static_cast<unsigned>(key),modifiers};
            if (!candidate.Valid()) return HotkeyEvent::Invalid;
            draft=candidate;
            recording=false;
            return HotkeyEvent::Changed;
        }
        if (!binding.key || key!=binding.key || modifiers!=binding.modifiers) return HotkeyEvent::Pass;
        blocked[key]=Scan(data);
        return HotkeyEvent::Toggle;
    }
};
