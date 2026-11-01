#pragma once
#include <windows.h>
#include <dbt.h>
#include <atomic>
#include <cstdint>

class ControllerDiscovery {
    inline static std::atomic<unsigned> changes{0};
    unsigned observed=changes.load(std::memory_order_relaxed);
    bool pending=true;
    uint64_t after=0;
public:
    static void Changed() { changes.fetch_add(1,std::memory_order_relaxed); }
    void Reset() { observed=changes.load(std::memory_order_relaxed); pending=true; after=0; }
    void Retry(uint64_t now) { pending=true; after=now+1000; }
    bool Due(uint64_t now) {
        const auto current=changes.load(std::memory_order_relaxed);
        if (observed!=current) { observed=current; pending=true; after=now+250; }
        if (!pending || now<after) return false;
        pending=false;
        return true;
    }
};

class ControllerDeviceNotifications {
    using Register=HDEVNOTIFY (WINAPI*)(HANDLE,LPVOID,DWORD);
    using Unregister=BOOL (WINAPI*)(HDEVNOTIFY);
    HDEVNOTIFY handle=nullptr;
    Unregister unregister=nullptr;
public:
    ~ControllerDeviceNotifications() { Reset(); }
    void Reset() { if (handle && unregister) unregister(handle); handle=nullptr; }
    bool Watch(HWND window) {
        Reset();
        const auto library=GetModuleHandleW(L"user32.dll");
        const auto subscribe=library ? reinterpret_cast<Register>(GetProcAddress(library,"RegisterDeviceNotificationW")) : nullptr;
        unregister=library ? reinterpret_cast<Unregister>(GetProcAddress(library,"UnregisterDeviceNotification")) : nullptr;
        if (!window || !subscribe || !unregister) return false;
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size=sizeof(filter);
        filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};
        handle=subscribe(window,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
        ControllerDiscovery::Changed();
        return handle!=nullptr;
    }
    static void Observe(UINT message,WPARAM event) {
        if (message==WM_DEVICECHANGE && (event==DBT_DEVNODES_CHANGED || event==DBT_DEVICEARRIVAL || event==DBT_DEVICEREMOVECOMPLETE))
            ControllerDiscovery::Changed();
    }
};
