#pragma once
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include "mythic_sounds.h"
#pragma comment(lib,"ole32.lib")

#include "custom_sound_device.h"

inline CustomSoundStatus CheckCustomSoundFile(const std::filesystem::path& path,bool checkExtension = true) {
    const auto extension = path.extension().native();
    if (checkExtension && _wcsicmp(extension.c_str(),L".wav") && _wcsicmp(extension.c_str(),L".mp3")) return CustomSoundStatus::Format;
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path,error);
    if (error || bytes < 12) return CustomSoundStatus::File;
    if (bytes > 32ull*1024*1024) return CustomSoundStatus::Size;
    std::ifstream input(path,std::ios::binary);
    std::array<unsigned char,12> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()),header.size())) return CustomSoundStatus::File;
    const bool wave = !std::memcmp(header.data(),"RIFF",4) && !std::memcmp(header.data()+8,"WAVE",4);
    const bool mp3 = !std::memcmp(header.data(),"ID3",3) || (header[0] == 0xff && (header[1]&0xe6) == 0xe2);
    if (checkExtension && ((!_wcsicmp(extension.c_str(),L".wav") && !wave) || (!_wcsicmp(extension.c_str(),L".mp3") && !mp3))) return CustomSoundStatus::Audio;
    return wave || mp3 ? CustomSoundStatus::Ready : CustomSoundStatus::Audio;
}

template<class Active>
inline CustomSoundStatus ImportCustomSoundFile(const std::filesystem::path& source,const std::filesystem::path& destination,Active active) {
        auto result = CheckCustomSoundFile(source);
        if (result != CustomSoundStatus::Ready) return result;
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(),error);
        if (error) return CustomSoundStatus::File;
        wchar_t temporary[MAX_PATH]{};
        if (!GetTempFileNameW(destination.parent_path().c_str(),L"snd",0,temporary)) return CustomSoundStatus::File;
        struct Cleanup { const wchar_t* path; ~Cleanup() { DeleteFileW(path); } } cleanup{temporary};
        std::ifstream input(source,std::ios::binary);
        std::ofstream output(temporary,std::ios::binary | std::ios::trunc);
        std::array<char,65536> buffer;
        uint64_t bytes = 0;
        while (input && active()) {
            input.read(buffer.data(),buffer.size());
            const auto count = input.gcount();
            bytes += static_cast<uint64_t>(count);
            if (bytes > 32ull*1024*1024) return CustomSoundStatus::Size;
            output.write(buffer.data(),count);
            if (!output) return CustomSoundStatus::File;
        }
        if (!input.eof()) return CustomSoundStatus::File;
        output.close(); input.close();
        if (!active() || !output || bytes < 12) return CustomSoundStatus::File;
        result = CheckCustomSoundFile(temporary,false);
        if (result != CustomSoundStatus::Ready) return result;
        CustomSoundDevice probe;
        if (!probe.Available()) return CustomSoundStatus::Unavailable;
        if (!probe.Open(temporary)) return CustomSoundStatus::Audio;
        probe.Close();
        if (!active() || !MoveFileExW(temporary,destination.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return CustomSoundStatus::File;
        return CustomSoundStatus::Ready;
}

class CustomMythicSound {
    enum class Action { Stop, Play, Browse };
    struct Request { Action action = Action::Stop; unsigned volume = 100; HWND owner = nullptr; uint64_t serial = 0; };
    struct State {
        std::filesystem::path path;
        HANDLE wake = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        std::mutex gate;
        Request request;
        std::atomic<uint64_t> serial{0};
        std::atomic<bool> stopping{false}, busy{false}, browsing{false}, exited{false}, imported{false};
        std::atomic<CustomSoundStatus> status{CustomSoundStatus::None};
        ~State() { if (wake) CloseHandle(wake); }
    };
    struct Start { std::shared_ptr<State> state; HMODULE module = nullptr; };
    std::shared_ptr<State> state;
    std::filesystem::path path;
    CustomSoundStatus initialStatus = CustomSoundStatus::None;
    static bool Current(const State& state,uint64_t serial) { return !state.stopping && state.serial == serial; }
    static void Browse(State& state,const Request& request) {
        state.browsing = true;
        state.status = CustomSoundStatus::Browsing;
        const auto library = LoadLibraryExW(L"comdlg32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto select = library ? reinterpret_cast<BOOL(WINAPI*)(LPOPENFILENAMEW)>(GetProcAddress(library,"GetOpenFileNameW")) : nullptr;
        if (!select) state.status = CustomSoundStatus::Unavailable;
        while (select && Current(state,request.serial)) {
            std::array<wchar_t,32768> file{};
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = request.owner;
            dialog.lpstrFilter = L"Sound files (*.wav;*.mp3)\0*.wav;*.mp3\0All files (*.*)\0*.*\0\0";
            dialog.nFilterIndex = 1; dialog.lpstrFile = file.data(); dialog.nMaxFile = DWORD(file.size());
            dialog.lpstrTitle = L"Select a custom Mythic drop sound";
            dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
            if (!select(&dialog)) {
                state.status = CheckCustomSoundFile(state.path,false) == CustomSoundStatus::Ready ? CustomSoundStatus::Ready : CustomSoundStatus::None;
                break;
            }
            if (!Current(state,request.serial)) break;
            const auto result = ImportCustomSoundFile(file.data(),state.path,[&] { return Current(state,request.serial); });
            state.status = result;
            if (result == CustomSoundStatus::Ready) state.imported = true;
            if (result == CustomSoundStatus::Ready || !Current(state,request.serial)) break;
            const char* message = CustomSoundMessage(result);
            std::wstring wide(message,message+std::strlen(message));
            MessageBoxW(request.owner,wide.c_str(),L"Mythic Drop Sounds",MB_OK | MB_ICONINFORMATION);
        }
        if (library) FreeLibrary(library);
        state.browsing = false;
    }
    static DWORD WINAPI Worker(void* pointer) {
        HMODULE module = nullptr;
        {
            std::unique_ptr<Start> start(static_cast<Start*>(pointer));
            auto state = start->state; module = start->module; start.reset();
            const HRESULT initialized = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            try {
                CustomSoundDevice device;
                uint64_t handled = 0;
                bool playing = false;
                while (!state->stopping) {
                    WaitForSingleObject(state->wake,playing ? 100 : INFINITE);
                    if (state->stopping) break;
                    Request request;
                    { std::lock_guard<std::mutex> lock(state->gate); request = state->request; }
                    if (request.serial != handled) {
                        handled = request.serial;
                        device.Close(); playing = false;
                        if (request.action == Action::Browse) Browse(*state,request);
                        else if (request.action == Action::Play && Current(*state,handled)) {
                            auto status = CheckCustomSoundFile(state->path,false);
                            if (status == CustomSoundStatus::Ready) {
                                if (!device.Available()) status = CustomSoundStatus::Unavailable;
                                else if (!device.Open(state->path) || !Current(*state,handled) || !device.Play(request.volume)) status = CustomSoundStatus::Audio;
                                else playing = true;
                            }
                            if (Current(*state,handled)) state->status = status;
                        }
                    }
                    if (playing && !device.Playing()) { device.Close(); playing = false; }
                    if (Current(*state,handled)) state->busy = playing;
                }
            } catch (...) { state->status = CustomSoundStatus::Unavailable; }
            state->busy = false; state->browsing = false;
            if (SUCCEEDED(initialized)) CoUninitialize();
            state->exited = true;
        }
        if (module) FreeLibraryAndExitThread(module,0);
        return 0;
    }
    bool Ensure() {
        if (state) return !state->exited;
        if (path.empty()) return false;
        auto created = std::make_shared<State>(); created->path = path; created->status = initialStatus;
        if (!created->wake) return false;
        auto start = std::make_unique<Start>(); start->state = created;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&start->module)) return false;
        HANDLE thread = CreateThread(nullptr,0,Worker,start.get(),0,nullptr);
        if (!thread) { FreeLibrary(start->module); return false; }
        start.release(); CloseHandle(thread); state = std::move(created);
        return true;
    }
    void Send(Action action,unsigned volume,HWND owner) {
        if (!Ensure()) return;
        std::lock_guard<std::mutex> lock(state->gate);
        const auto serial = ++state->serial;
        state->request = {action,volume,owner,serial};
        state->busy = action != Action::Stop;
        if (action == Action::Browse) state->browsing = true;
        SetEvent(state->wake);
    }
public:
    ~CustomMythicSound() { if (state) { state->stopping = true; SetEvent(state->wake); } }
    void Configure(const std::filesystem::path& directory) {
        if (state) return;
        path = directory/L"custom.audio";
        initialStatus = CheckCustomSoundFile(path,false) == CustomSoundStatus::Ready ? CustomSoundStatus::Ready : CustomSoundStatus::None;
    }
    void Play(unsigned volume) { Send(Action::Play,volume,nullptr); }
    void Select(HWND owner) { if (!Browsing()) Send(Action::Browse,0,owner); }
    void Stop() { if (state && Busy() && !Browsing()) Send(Action::Stop,0,nullptr); }
    bool Busy() const { return state && state->busy; }
    bool Browsing() const { return state && state->browsing; }
    bool TakeImported() { return state && state->imported.exchange(false); }
    CustomSoundStatus Status() const { return state ? state->status.load() : initialStatus; }
};
