#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

template<class Record> class HistoryWriter {
public:
    using Records = std::vector<std::shared_ptr<const Record>>;
    using WriteFunction = DWORD (*)(const std::filesystem::path&,uint32_t,const Records&);
    static DWORD Write(const std::filesystem::path& file,uint32_t magic,const Records& records) {
        if constexpr (!std::is_trivially_copyable_v<Record>) {
            return Record::WriteHistory(file,magic,records);
        } else {
        uint32_t checksum=2166136261u;
        for (const auto& record:records) {
            const auto* bytes=reinterpret_cast<const unsigned char*>(record.get());
            for (size_t i=0;i<sizeof(Record);++i) checksum=(checksum^bytes[i])*16777619u;
        }
        const uint32_t header[]={magic,sizeof(Record),static_cast<uint32_t>(records.size()),checksum};
        auto historyPending=file; historyPending+=L".pending";
        std::ofstream historyFile(historyPending,std::ios::binary|std::ios::trunc);
        historyFile.write(reinterpret_cast<const char*>(header),sizeof(header));
        for (const auto& record:records) historyFile.write(reinterpret_cast<const char*>(record.get()),sizeof(Record));
        historyFile.close();
        if (!historyFile.good()) return ERROR_WRITE_FAULT;
        if (!MoveFileExW(historyPending.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return GetLastError();
        return ERROR_SUCCESS;
        }
    }
private:
    struct State {
        std::filesystem::path file;
        uint32_t magic=0;
        WriteFunction write=nullptr;
        HANDLE owner=INVALID_HANDLE_VALUE, idle=nullptr;
        std::mutex gate;
        Records pending;
        bool queued=false, running=false;
        std::atomic<DWORD> error{ERROR_SUCCESS};
        ~State() {
            if (owner!=INVALID_HANDLE_VALUE) CloseHandle(owner);
            if (idle) CloseHandle(idle);
        }
    };
    std::shared_ptr<State> state;
    static void CALLBACK Run(PTP_CALLBACK_INSTANCE instance,void* callbackData) noexcept {
        std::unique_ptr<std::shared_ptr<State>> lifetime(static_cast<std::shared_ptr<State>*>(callbackData));
        auto& data=**lifetime;
        CallbackMayRunLong(instance);
        for (;;) {
            Records records;
            {
                std::lock_guard<std::mutex> lock(data.gate);
                if (!data.queued) { data.running=false; SetEvent(data.idle); return; }
                records=std::move(data.pending);
                data.queued=false;
            }
            DWORD error=ERROR_WRITE_FAULT;
            try { error=data.write(data.file,data.magic,records); } catch (...) {}
            if (error!=ERROR_SUCCESS) {
                Records discarded;
                {
                    std::lock_guard<std::mutex> lock(data.gate);
                    data.error.store(error);
                    discarded=std::move(data.pending);
                    data.queued=data.running=false;
                    SetEvent(data.idle);
                }
                return;
            }
        }
    }
    static bool PinCallback() {
        static const bool pinned=[] {
            HMODULE module=nullptr;
            return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Run),&module)!=FALSE;
        }();
        return pinned;
    }
public:
    explicit HistoryWriter(const std::filesystem::path& file,uint32_t magic,WriteFunction write=Write):state(std::make_shared<State>()) {
        state->file=file; state->magic=magic; state->write=write;
        state->idle=CreateEventW(nullptr,TRUE,TRUE,nullptr);
        if (!state->idle) { state->error.store(GetLastError()); return; }
        auto lockFile=file; lockFile+=L".lock";
        state->owner=CreateFileW(lockFile.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (state->owner==INVALID_HANDLE_VALUE) state->error.store(GetLastError());
    }
    HistoryWriter(const HistoryWriter&)=delete;
    HistoryWriter& operator=(const HistoryWriter&)=delete;
    bool Available() const { return state->owner!=INVALID_HANDLE_VALUE && state->idle; }
    DWORD Error() const { return state->error.load(); }
    bool Queue(Records records) {
        if (!Available() || Error()!=ERROR_SUCCESS || records.size()>20) return false;
        for (const auto& record:records) if (!record) return false;
        if (!PinCallback()) { state->error.store(ERROR_DLL_INIT_FAILED); return false; }
        auto callbackData=std::make_unique<std::shared_ptr<State>>(state);
        Records retired;
        std::lock_guard<std::mutex> lock(state->gate);
        if (Error()!=ERROR_SUCCESS) return false;
        retired=std::move(state->pending);
        state->pending=std::move(records); state->queued=true;
        if (state->running) return true;
        state->running=true;
        ResetEvent(state->idle);
        if (!TrySubmitThreadpoolCallback(Run,callbackData.get(),nullptr)) {
            state->error.store(GetLastError());
            retired=std::move(state->pending); state->queued=state->running=false;
            SetEvent(state->idle);
            return false;
        }
        callbackData.release();
        return true;
    }
    bool Wait(DWORD milliseconds) const {
        if (!Available()) return false;
        const DWORD begin=GetTickCount();
        if (WaitForSingleObject(state->idle,milliseconds)!=WAIT_OBJECT_0) return false;
        while (state.use_count()>1) {
            if (GetTickCount()-begin>=milliseconds) return false;
            Sleep(1);
        }
        return Error()==ERROR_SUCCESS;
    }
};
