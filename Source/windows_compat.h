#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <type_traits>

namespace WindowsCompat {
constexpr UINT MouseHorizontalWheel=0x020e;
template<class T> class ThreadData {
    static_assert(std::is_trivial<T>::value,"Thread data must be trivial");
    DWORD slot=TLS_OUT_OF_INDEXES;
public:
    bool Initialize() {slot=TlsAlloc();return slot!=TLS_OUT_OF_INDEXES;}
    T* Get() {
        if(slot==TLS_OUT_OF_INDEXES)return nullptr;
        const DWORD error=GetLastError();
        auto* data=static_cast<T*>(TlsGetValue(slot));
        if(!data) {
            data=static_cast<T*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(T)));
            if(data&&!TlsSetValue(slot,data)){HeapFree(GetProcessHeap(),0,data);data=nullptr;}
        }
        SetLastError(error);return data;
    }
    void ReleaseCurrent() {
        if(slot==TLS_OUT_OF_INDEXES)return;
        auto* data=TlsGetValue(slot);if(data){TlsSetValue(slot,nullptr);HeapFree(GetProcessHeap(),0,data);}
    }
    void Shutdown() {ReleaseCurrent();if(slot!=TLS_OUT_OF_INDEXES){TlsFree(slot);slot=TLS_OUT_OF_INDEXES;}}
};
class Once {
    volatile LONG state=0;
public:
    template<class Function> bool Run(Function initialize) {
        for (;;) {
            const LONG previous=InterlockedCompareExchange(&state,1,0);
            if (previous==2) return true;
            if (previous==0) {
                try {
                    const bool initialized=initialize()!=FALSE;
                    InterlockedExchange(&state,initialized ? 2 : 0);
                    return initialized;
                } catch (...) { InterlockedExchange(&state,0); throw; }
            }
            Sleep(1);
        }
    }
};
inline uint64_t ExtendTick(uint64_t previous,DWORD current) {
    if (!previous) return current;
    const DWORD elapsed=current-static_cast<DWORD>(previous);
    return elapsed<0x80000000u ? previous+elapsed : previous;
}
inline uint64_t Milliseconds() {
    alignas(8) static volatile LONG64 elapsed=0;
    const DWORD current=GetTickCount();
    for (;;) {
        const LONG64 before=InterlockedCompareExchange64(&elapsed,0,0);
        const LONG64 after=static_cast<LONG64>(ExtendTick(static_cast<uint64_t>(before),current));
        if (InterlockedCompareExchange64(&elapsed,after,before)==before) return static_cast<uint64_t>(after);
    }
}
inline HMODULE LoadLibrary(const wchar_t* path) {
    if (!path || !(path[0] && path[1]==L':' && (path[2]==L'\\'||path[2]==L'/')) && !(path[0]==L'\\'&&path[1]==L'\\')) { SetLastError(ERROR_BAD_PATHNAME); return nullptr; }
    const HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
    using GetMode=DWORD (WINAPI*)();
    using SetMode=BOOL (WINAPI*)(DWORD,LPDWORD);
    const auto getMode=reinterpret_cast<GetMode>(GetProcAddress(kernel,"GetThreadErrorMode"));
    const auto setMode=reinterpret_cast<SetMode>(GetProcAddress(kernel,"SetThreadErrorMode"));
    DWORD previous=0;
    const bool changed=getMode && setMode && setMode(getMode()|SEM_FAILCRITICALERRORS,&previous);
    const DWORD flags=GetProcAddress(kernel,"AddDllDirectory") ? LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32 : LOAD_WITH_ALTERED_SEARCH_PATH;
    const HMODULE module=LoadLibraryExW(path,nullptr,flags);
    const DWORD error=GetLastError();
    if (changed) setMode(previous,nullptr);
    SetLastError(error);
    return module;
}
inline HMODULE SystemLibrary(const wchar_t* name) {
    if (!name || std::wcspbrk(name,L"\\/:")) { SetLastError(ERROR_BAD_PATHNAME); return nullptr; }
    wchar_t path[MAX_PATH]{};
    const DWORD size=GetSystemDirectoryW(path,MAX_PATH);
    if (!size || size+1+std::wcslen(name)>=MAX_PATH) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return nullptr; }
    path[size]=L'\\';std::wmemcpy(path+size+1,name,std::wcslen(name)+1);
    return LoadLibrary(path);
}
}
