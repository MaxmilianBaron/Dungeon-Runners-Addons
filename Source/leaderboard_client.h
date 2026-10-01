#pragma once
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include "leaderboard.h"

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
class InternetHandle {
    HINTERNET value;
public:
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    operator HINTERNET() const { return value; }
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
    const uint64_t deadline=GetTickCount64()+12000;
    InternetHandle session(WinHttpOpen(L"Dungeon-Runners-Addons/Leaderboard",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if (!session || !WinHttpSetTimeouts(session,2500,2500,2500,2500)) return result;
    DWORD protocols=WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!WinHttpSetOption(session,WINHTTP_OPTION_SECURE_PROTOCOLS,&protocols,sizeof(protocols))) return result;
    InternetHandle connection(WinHttpConnect(session,L"play.dungeonrunnersreborn.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    if (!connection || cancelled) return result;
    const wchar_t* accept[]={L"application/json",nullptr};
    InternetHandle request(WinHttpOpenRequest(connection,L"GET",paths[index],nullptr,WINHTTP_NO_REFERER,accept,WINHTTP_FLAG_SECURE));
    if (!request) return result;
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
    DWORD headerLimit=16384;
    if (!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)) ||
        !WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)) ||
        !WinHttpSetOption(request,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,&headerLimit,sizeof(headerLimit))) return result;
    if (cancelled) return result;
    bool sent=WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)!=FALSE;
    if (!sent && GetLastError()==ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED && !cancelled &&
        WinHttpSetOption(request,WINHTTP_OPTION_CLIENT_CERT_CONTEXT,WINHTTP_NO_CLIENT_CERT_CONTEXT,0))
        sent=WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)!=FALSE;
    if (!sent || cancelled || !WinHttpReceiveResponse(request,nullptr)) return result;
    DWORD status=0,size=sizeof(status);
    if (!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)) return result;
    if (status==429 || status==503) {
        wchar_t retry[128]{}; size=sizeof(retry);
        if (WinHttpQueryHeaders(request,WINHTTP_QUERY_RETRY_AFTER,WINHTTP_HEADER_NAME_BY_INDEX,retry,&size,WINHTTP_NO_HEADER_INDEX)) result.retryMilliseconds=RetryDelay(retry);
        result.error="Official service is busy. Refresh will be available after the retry delay.";
        return result;
    }
    if (status!=200) { result.error="Official service returned HTTP "+std::to_string(status)+". Cached data was kept."; result.retryMilliseconds=300000; return result; }
    wchar_t type[128]{}; size=sizeof(type);
    if (!WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,type,&size,WINHTTP_NO_HEADER_INDEX)) return result;
    std::wstring contentType(type);
    for (auto& c:contentType) if (c>=L'A' && c<=L'Z') c=static_cast<wchar_t>(c-L'A'+L'a');
    if (contentType!=L"application/json" && contentType.rfind(L"application/json;",0)!=0) { result.error="Official service returned an unsupported response."; return result; }
    std::string body;
    char buffer[4096];
    while (!cancelled && GetTickCount64()<deadline) {
        DWORD read=0;
        if (!WinHttpReadData(request,buffer,sizeof(buffer),&read)) return result;
        if (!read) { result.body=std::move(body); result.error.clear(); result.retryMilliseconds=15000; return result; }
        if (body.size()+read>BodyLimit) { result.error="Official response is too large. Cached data was kept."; return result; }
        body.append(buffer,read);
    }
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
        Clock clock=[]() -> uint64_t { return GetTickCount64(); };
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
