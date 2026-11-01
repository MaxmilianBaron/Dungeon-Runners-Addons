#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdint>
#include <cstring>

struct NativePatchSpec {
    void* address;
    void* handler;
    void** original;
    const char* expected;
    size_t length;
};

class NativePatchSet {
    struct Patch {
        NativePatchSpec spec{};
        unsigned char saved[64]{};
        size_t expectedSize=0;
        void* trampoline=nullptr;
        DWORD protection=0;
    };
    std::array<Patch,24> patches{};
    size_t count=0;
    bool installed=false;
    static bool Read(const void* address,void* result,size_t size) {
        __try { std::memcpy(result,address,size); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool Decode(const char* hex,unsigned char* bytes,size_t& size) {
        if (!hex) return false;
        const size_t length=std::strlen(hex);
        if (!length || length%2 || length>128) return false;
        size=length/2;
        for (size_t i=0;i<length;++i) {
            const char c=hex[i]; const unsigned value=c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : 255;
            if (value>15) return false;
            if (!(i%2)) bytes[i/2]=static_cast<unsigned char>(value<<4); else bytes[i/2]|=static_cast<unsigned char>(value);
        }
        return true;
    }
    static void Jump(unsigned char* at,const void* to) {
        at[0]=0xe9;
        const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(to)-reinterpret_cast<uintptr_t>(at)-5);
        std::memcpy(at+1,&relative,4);
    }
    struct Threads {
        std::array<HANDLE,1024> handles{};
        size_t count=0,suspended=0;
        ~Threads() { while (suspended) ResumeThread(handles[--suspended]); for (size_t i=0;i<count;++i) CloseHandle(handles[i]); }
        bool Gather() {
            HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
            if (snapshot==INVALID_HANDLE_VALUE) return false;
            THREADENTRY32 entry{}; entry.dwSize=sizeof(entry); bool valid=Thread32First(snapshot,&entry)!=FALSE;
            if (valid) do {
                if (entry.th32OwnerProcessID!=GetCurrentProcessId() || entry.th32ThreadID==GetCurrentThreadId()) continue;
                if (count==handles.size()) { valid=false; break; }
                HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
                if (!thread) { if (GetLastError()==ERROR_INVALID_PARAMETER) continue; valid=false; break; }
                handles[count++]=thread;
            } while (Thread32Next(snapshot,&entry));
            CloseHandle(snapshot); return valid;
        }
        bool Freeze() {
            for (;suspended<count;++suspended) if (SuspendThread(handles[suspended])==static_cast<DWORD>(-1)) return false;
            return true;
        }
    };
public:
    ~NativePatchSet() { if (!installed) for (auto& patch:patches) if (patch.trampoline) VirtualFree(patch.trampoline,0,MEM_RELEASE); }
    bool Install(const NativePatchSpec* specs,size_t size) {
        static_assert(sizeof(void*)==4);
        if (installed || count || !specs || !size || size>patches.size()) return false;
        count=size;
        for (size_t i=0;i<size;++i) {
            auto& patch=patches[i]; patch.spec=specs[i];
            const auto& spec=patch.spec;
            if (!spec.address || !spec.handler || !spec.original || spec.length<5 || spec.length>32 || !Decode(spec.expected,patch.saved,patch.expectedSize) || patch.expectedSize<spec.length) return false;
            unsigned char actual[64]{};
            if (!Read(spec.address,actual,patch.expectedSize) || std::memcmp(actual,patch.saved,patch.expectedSize)) return false;
            const auto start=reinterpret_cast<uintptr_t>(spec.address);
            if (start>UINTPTR_MAX-patch.expectedSize) return false;
            for (size_t j=0;j<i;++j) {
                const auto other=reinterpret_cast<uintptr_t>(patches[j].spec.address);
                if (start<other+patches[j].spec.length && other<start+spec.length) return false;
            }
            patch.trampoline=VirtualAlloc(nullptr,spec.length+5,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if (!patch.trampoline) return false;
            std::memcpy(patch.trampoline,patch.saved,spec.length);
            Jump(static_cast<unsigned char*>(patch.trampoline)+spec.length,static_cast<unsigned char*>(spec.address)+spec.length);
            DWORD old=0;
            if (!VirtualProtect(patch.trampoline,spec.length+5,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),patch.trampoline,spec.length+5)) return false;
        }
        Threads threads;
        if (!threads.Gather() || !threads.Freeze()) return false;
        for (size_t i=0;i<threads.count;++i) {
            CONTEXT context{}; context.ContextFlags=CONTEXT_CONTROL;
            if (!GetThreadContext(threads.handles[i],&context)) return false;
            for (size_t j=0;j<size;++j) {
                const auto start=reinterpret_cast<uintptr_t>(specs[j].address);
                if (context.Eip>=start && context.Eip<start+specs[j].length) return false;
            }
        }
        for (size_t i=0;i<size;++i) {
            unsigned char actual[64]{};
            if (!Read(specs[i].address,actual,patches[i].expectedSize) || std::memcmp(actual,patches[i].saved,patches[i].expectedSize)) return false;
        }
        size_t writable=0;
        for (;writable<size;++writable) if (!VirtualProtect(specs[writable].address,specs[writable].length,PAGE_EXECUTE_READWRITE,&patches[writable].protection)) break;
        bool success=writable==size;
        if (success) {
            for (size_t i=0;i<size;++i) *specs[i].original=patches[i].trampoline;
            for (size_t i=0;i<size;++i) {
                auto* code=static_cast<unsigned char*>(specs[i].address);
                Jump(code,specs[i].handler);
                for (size_t n=5;n<specs[i].length;++n) code[n]=0x90;
            }
            success=FlushInstructionCache(GetCurrentProcess(),nullptr,0)!=FALSE;
            if (!success) {
                for (size_t i=0;i<size;++i) { std::memcpy(specs[i].address,patches[i].saved,specs[i].length); *specs[i].original=nullptr; }
                FlushInstructionCache(GetCurrentProcess(),nullptr,0);
            }
        }
        bool restored=true;
        while (writable) { --writable; DWORD old=0; if (!VirtualProtect(specs[writable].address,specs[writable].length,patches[writable].protection,&old)) restored=false; }
        installed=success;
        return success && restored;
    }
};
