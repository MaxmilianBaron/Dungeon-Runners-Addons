#pragma once
#include "windows_compat.h"
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include "dualsense.h"

class NativeDualSense {
    IDirectInput8W* input=nullptr;
    IDirectInputDevice8W* device=nullptr;
    bool attempted=false;
    HWND owner=nullptr;
    uint64_t nextProbe=0;
    unsigned generation=0;
    bool Open(const DIDEVICEINSTANCEW& instance) {
        if (!Controller::DualSenseProduct(LOWORD(instance.guidProduct.Data1),HIWORD(instance.guidProduct.Data1))) return false;
        IDirectInputDevice8W* candidate=nullptr;
        if (FAILED(input->CreateDevice(instance.guidInstance,&candidate,nullptr))) return false;
        DIPROPRANGE range{}; range.diph.dwSize=sizeof(range); range.diph.dwHeaderSize=sizeof(range.diph); range.diph.dwHow=DIPH_DEVICE;
        range.lMin=-32768; range.lMax=32767;
        if (FAILED(candidate->SetDataFormat(&c_dfDIJoystick2)) || FAILED(candidate->SetCooperativeLevel(owner,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND)) ||
            FAILED(candidate->SetProperty(DIPROP_RANGE,&range.diph))) { candidate->Release(); return false; }
        DIPROPDWORD deadzone{}; deadzone.diph.dwSize=sizeof(deadzone); deadzone.diph.dwHeaderSize=sizeof(deadzone.diph); deadzone.diph.dwHow=DIPH_DEVICE;
        candidate->SetProperty(DIPROP_DEADZONE,&deadzone.diph);
        if (FAILED(candidate->Acquire())) { candidate->Release(); return false; }
        device=candidate; ++generation;
        return true;
    }
    static BOOL CALLBACK Enumerate(const DIDEVICEINSTANCEW* instance,void* context) {
        return static_cast<NativeDualSense*>(context)->Open(*instance) ? DIENUM_STOP : DIENUM_CONTINUE;
    }
public:
    NativeDualSense()=default;
    NativeDualSense(const NativeDualSense&)=delete;
    NativeDualSense& operator=(const NativeDualSense&)=delete;
    ~NativeDualSense() { Disconnect(); if (input) input->Release(); }
    unsigned Generation() const { return generation; }
    void Disconnect() {
        if (device) { device->Unacquire(); device->Release(); device=nullptr; }
        owner=nullptr;
    }
    Controller::Pad Read(HWND window,uint64_t now) {
        if (!window || !IsWindow(window)) return {};
        if (!attempted) {
            attempted=true;
            const auto ntdll=GetModuleHandleW(L"ntdll.dll");
            if (ntdll && GetProcAddress(ntdll,"wine_get_version")) return {};
            const auto module=WindowsCompat::SystemLibrary(L"dinput8.dll");
            using Create=HRESULT (WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
            const auto create=module ? reinterpret_cast<Create>(GetProcAddress(module,"DirectInput8Create")) : nullptr;
            if (create) create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr);
            else if (module) FreeLibrary(module);
        }
        if (!input) return {};
        if (device && owner!=window) Disconnect();
        if (!device) {
            if (now<nextProbe) return {};
            nextProbe=now+1000; owner=window;
            input->EnumDevices(DI8DEVCLASS_GAMECTRL,Enumerate,this,DIEDFL_ATTACHEDONLY);
            if (!device) return {};
        }
        DIJOYSTATE2 value{};
        auto result=device->Poll();
        if (SUCCEEDED(result)) result=device->GetDeviceState(sizeof(value),&value);
        if (result==DIERR_INPUTLOST || result==DIERR_NOTACQUIRED) {
            result=device->Acquire();
            if (SUCCEEDED(result)) { result=device->Poll(); if (SUCCEEDED(result)) result=device->GetDeviceState(sizeof(value),&value); }
        }
        if (FAILED(result)) { Disconnect(); nextProbe=now+1000; return {}; }
        Controller::DualSenseState state;
        state.axes={{value.lX,value.lY,value.lZ,value.lRx,value.lRy,value.lRz}};
        state.pov=value.rgdwPOV[0];
        for (unsigned bit=0;bit<12;++bit) if (value.rgbButtons[bit]&0x80) state.buttons|=1u<<bit;
        return Controller::DualSensePad(state);
    }
};
