#pragma once
#include <windows.h>
#include "windows_compat.h"
#include <winhttp.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include "leaderboard.h"
#include "https_client.h"

namespace Leaderboard {
struct Result {
    std::shared_ptr<const Board> board;
    std::string error;
    uint64_t received = 0, nextAttempt = 0;
    bool loading = false;
};
struct Download {
    std::string body, error;
    uint64_t retryMilliseconds = 60000;
};
inline uint64_t RetryDelay(const std::wstring& text) {
    uint64_t seconds=0;
    bool digits=!text.empty();
    for (const auto c:text) {
        if (c<'0' || c>'9') { digits=false; break; }
        seconds=std::min<uint64_t>(31536000,seconds*10+c-'0');
    }
    if (digits) return std::max<uint64_t>(15000,seconds*1000);
    SYSTEMTIME date{}; FILETIME future{}, now{};
    if (WinHttpTimeToSystemTime(text.c_str(),&date) && SystemTimeToFileTime(&date,&future)) {
        GetSystemTimeAsFileTime(&now);
        const uint64_t end=(static_cast<uint64_t>(future.dwHighDateTime)<<32)|future.dwLowDateTime;
        const uint64_t start=(static_cast<uint64_t>(now.dwHighDateTime)<<32)|now.dwLowDateTime;
        if (end>start) return std::max<uint64_t>(15000,(end-start)/10000);
    }
    return 60000;
}
inline Download Fetch(Feed feed,const std::atomic<bool>& cancelled) {
    Download result; result.error="Cannot reach the official leaderboard. Try again later.";
    const wchar_t* paths[]={L"/api/level",L"/api/gold",L"/api/played",L"/api/pvp",L"/api/pvp?period=week"};
    const auto index=static_cast<unsigned>(feed);
    if (index>=static_cast<unsigned>(Feed::Count) || cancelled) return result;
    try {
        const std::wstring selected=paths[index];
        const auto response=AddonHttps::Get("play.dungeonrunnersreborn.com",std::string(selected.begin(),selected.end()),cancelled);
        if (response.status==429 || response.status==503) {
            if (!response.retryAfter.empty()) result.retryMilliseconds=RetryDelay(std::wstring(response.retryAfter.begin(),response.retryAfter.end()));
            result.error="Official service is busy. Refresh will be available after the retry delay.";
        } else if (response.status!=200) {
            result.error="Official service returned HTTP "+std::to_string(response.status)+". Cached data was kept.";
            result.retryMilliseconds=300000;
        } else if (response.contentType!="application/json" && response.contentType.rfind("application/json;",0)!=0) {
            result.error="Official service returned an unsupported response.";
        } else if (!cancelled) {
            result.body=response.body;result.error.clear();result.retryMilliseconds=15000;
        }
    } catch (const std::exception&) {}
    return result;
}

class Client {
public:
    using Transport=Download (*)(Feed,const std::atomic<bool>&);
    using Clock=uint64_t (*)();
private:
    struct State {
        std::mutex gate;
        std::atomic<bool> stopped{false};
        std::array<Result,static_cast<size_t>(Feed::Count)> results;
        bool busy=false;
        uint64_t nextRequest=0;
        Transport transport=Fetch;
        Clock clock=[]() -> uint64_t { return WindowsCompat::Milliseconds(); };
    };
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    explicit Client(Transport transport=Fetch,Clock clock=nullptr) { state->transport=transport ? transport : Fetch; if (clock) state->clock=clock; }
    ~Client() { Stop(); }
    void Stop() { state->stopped=true; }
    Result Read(Feed feed) const {
        const auto index=static_cast<size_t>(feed);
        if (index>=state->results.size()) return {};
        std::lock_guard<std::mutex> lock(state->gate);
        auto result=state->results[index];
        result.nextAttempt=std::max(result.nextAttempt,state->nextRequest);
        result.loading=result.loading || (state->busy && !result.board);
        return result;
    }
    void Request(Feed feed,bool force=false) {
        const auto shared=state;
        const auto index=static_cast<size_t>(feed);
        const uint64_t now=shared->clock();
        std::lock_guard<std::mutex> lock(shared->gate);
        if (index>=shared->results.size() || shared->stopped || shared->busy || now<shared->nextRequest) return;
        auto& slot=shared->results[index];
        if (now<slot.nextAttempt || (!force && slot.board && now-slot.received<60000)) return;
        slot.loading=true; shared->busy=true;
        try {
            std::thread([shared,feed,index] {
                Download download; std::shared_ptr<const Board> board;
                try {
                    download=shared->transport(feed,shared->stopped);
                    if (download.error.empty()) board=std::make_shared<Board>(Parse(download.body,feed));
                } catch (...) { download.error="Official data could not be read. Cached data was kept."; download.retryMilliseconds=60000; }
                std::lock_guard<std::mutex> guard(shared->gate);
                auto& entry=shared->results[index];
                const uint64_t done=shared->clock();
                entry.loading=false; shared->busy=false;
                shared->nextRequest=done+(download.error.empty() ? 1000 : download.retryMilliseconds);
                if (shared->stopped) return;
                entry.nextAttempt=done+download.retryMilliseconds;
                entry.error=std::move(download.error);
                if (board) { entry.board=std::move(board); entry.received=done; }
            }).detach();
        } catch (...) {
            slot.loading=false; shared->busy=false; slot.nextAttempt=now+60000;
            slot.error="Leaderboard is temporarily unavailable.";
        }
    }
};
}
