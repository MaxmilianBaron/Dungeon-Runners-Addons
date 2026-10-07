#pragma once
#include <cstdint>

class MouseLookGesture {
    uintptr_t handler = 0, mouse = 0;
    bool active = false, armed = false;
public:
    void Reset() { handler = mouse = 0; active = armed = false; }
    bool Update(uintptr_t nextHandler,uintptr_t nextMouse,bool enabled,bool available,bool right,bool worldPoint,bool start) {
        if (handler != nextHandler || mouse != nextMouse) {
            active = false;
            armed = !right;
            handler = nextHandler;
            mouse = nextMouse;
        }
        if (!enabled || !available || !handler || !mouse) { active = false; armed = false; return false; }
        if (!right) { active = false; armed = true; return false; }
        if (!active && !worldPoint) armed = false;
        if (!active && armed && start) { active = worldPoint; armed = false; }
        return active;
    }
    bool Owns(uintptr_t currentHandler,uintptr_t currentMouse) const {
        return active && handler == currentHandler && mouse == currentMouse;
    }
    bool Active() const { return active; }
    uintptr_t Handler() const { return handler; }
    uintptr_t Mouse() const { return mouse; }
};
